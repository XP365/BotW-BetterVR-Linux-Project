#include "instance.h"
#include "cemu_hooks.h"
#include "rendering/openxr.h"


void CemuHooks::hook_BeginCameraSide(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    OpenXR::EyeSide side = hCPU->gpr[0] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;


    Log::print<RENDERING>("");
    Log::print<RENDERING>("===============================================================================");
    Log::print<RENDERING>("{0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0}", side == OpenXR::EyeSide::LEFT ? "LEFT" : "RIGHT");

    bool layersInitialized = VRManager::instance().XR->GetRenderer()->m_layer3D && VRManager::instance().XR->GetRenderer()->m_layer2D;
    if (layersInitialized && side == OpenXR::EyeSide::LEFT) {
        if (VRManager::instance().VK->m_imguiOverlay) {
            VRManager::instance().VK->m_imguiOverlay->BeginFrame();
            VRManager::instance().VK->m_imguiOverlay->Update();
        }
        VRManager::instance().XR->GetRenderer()->StartFrame();
    }
}

static std::pair<glm::quat, glm::quat> swingTwistY(const glm::quat& q) {
    glm::vec3 yAxis(0, 1, 0);
    glm::vec3 r(q.x, q.y, q.z);
    float dot = glm::dot(r, yAxis);
    glm::vec3 proj = yAxis * dot;
    glm::quat twist = glm::normalize(glm::quat(q.w, proj.x, proj.y, proj.z));
    glm::quat swing = q * glm::conjugate(twist);
    return { swing, twist };
}

glm::fvec3 s_wsCameraPosition = glm::fvec3();
glm::fquat s_wsCameraRotation = glm::identity<glm::fquat>();

// 0x18000021 for carrying the electricity balls at least 
// 0x00000010 for ladder
// 0x00000012 for ladder transition
// 0x18000002 for jumping
// 0x00008002 for gliding
// 0x00000090 for climbing
enum class PlayerMoveBitFlags : uint32_t {
    IS_MOVING = 1 << 0,
    UNK_02 = 1 << 1,
    UNK_004 = 1 << 2,
    UNK_008 = 1 << 3,
    UNK_016 = 1 << 4,
    UNK_032 = 1 << 5,
    UNK_064 = 1 << 6,
    UNK_128 = 1 << 7,
    UNK_256 = 1 << 8,
    UNK_512 = 1 << 9,
    UNK_1024 = 1 << 10,
    UNK_2048 = 1 << 11,
    UNK_4096 = 1 << 12,
    UNK_8192 = 1 << 13,
    UNK_16384 = 1 << 14,
    UNK_32768 = 1 << 15,
    UNK_65536 = 1 << 16,
    UNK_131072 = 1 << 17,
    UNK_262144 = 1 << 18,
    UNK_524288 = 1 << 19,
    UNK_1048576 = 1 << 20,
    UNK_2097152 = 1 << 21,
    UNK_4194304 = 1 << 22,
    UNK_8388608 = 1 << 23,
    UNK_16777216 = 1 << 24,
    UNK_33554432 = 1 << 25,
    UNK_67108864 = 1 << 26,
    UNK_134217728 = 1 << 27, // GETS DISABLED WHEN INSIDE A CUTSCENE
    UNK_268435456 = 1 << 28,
    UNK_536870912 = 1 << 29,
    UNK_1073741824 = 1 << 30,
    GET_HIT_MAYBE_UNABLE_TO_INTERACT = 1u << 31
};

void CemuHooks::hook_UpdateCameraForGameplay(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    if (CemuHooks::UseBlackBarsDuringEvents()) {
        return;
    }

    // read the camera matrix from the game's memory
    uint32_t ppc_cameraMatrixOffsetIn = hCPU->gpr[31];
    OpenXR::EyeSide ppc_cameraSide = hCPU->gpr[3] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;
    ActCamera actCam = {};
    readMemory(ppc_cameraMatrixOffsetIn, &actCam);

    // extract components from the existing camera matrix
    glm::fvec3 oldCameraPosition = actCam.finalCamMtx.pos.getLE();
    glm::fvec3 oldCameraTarget = actCam.finalCamMtx.target.getLE();
    glm::fvec3 oldCameraForward = glm::normalize(oldCameraTarget - oldCameraPosition);
    glm::fvec3 oldCameraUp = actCam.finalCamMtx.up.getLE();
    glm::fvec3 oldCameraUnknown = actCam.finalCamMtx.unknown.getLE();    
    float extraValue0 = actCam.finalCamMtx.zNear.getLE();
    float extraValue1 = actCam.finalCamMtx.zFar.getLE();

    // remove verticality from the camera position to avoid pitch changes that aren't from the VR headset
    oldCameraPosition.y = oldCameraTarget.y;

    // construct glm matrix from the existing camera parameters
    glm::mat4 existingGameMtx = glm::lookAtRH(oldCameraPosition, oldCameraTarget, oldCameraUp);

    // pass real camera position and rotation to be used elsewhere
    // GetRenderCamera recalculates the left and right eye positions from the base camera position
    s_wsCameraPosition = oldCameraPosition;
    s_wsCameraRotation = glm::quat_cast(glm::inverse(existingGameMtx));

    // rebase the rotation to the player position
    if (IsFirstPerson()) {
        BEMatrix34 mtx = {};
        readMemory(s_playerMtxAddress, &mtx);
        glm::fvec3 playerPos = mtx.getPos().getLE();

        if (auto settings = GetFirstPersonSettingsForActiveEvent()) {
            if (settings->ignoreCameraRotation) {
                glm::fquat playerRot = mtx.getRotLE();
                s_wsCameraRotation = playerRot; // * glm::angleAxis(glm::radians(180.0f), glm::fvec3(0.0f, 1.0f, 0.0f));
            }
        }

        glm::mat4 playerMtx4 = glm::inverse(glm::translate(glm::identity<glm::mat4>(), playerPos) * glm::mat4(s_wsCameraRotation));
        existingGameMtx = playerMtx4;
    }

    // current VR headset camera matrix
    auto viewsOpt = VRManager::instance().XR->GetRenderer()->GetMiddlePose();
    if (!viewsOpt) {
        Log::print<ERROR>("hook_UpdateCameraForGameplay: No views available for the middle pose.");
        return;
    }
    auto& views = viewsOpt.value();

    // calculate final camera matrix
    glm::mat4 finalPose = glm::inverse(existingGameMtx) * views;

    // extract camera up, forward and position from the final matrix
    glm::fvec3 camPos = glm::fvec3(finalPose[3]);
    glm::fvec3 forward = -glm::normalize(glm::fvec3(finalPose[2]));
    glm::fvec3 up = glm::normalize(glm::fvec3(finalPose[1]));

    float oldCameraDistance = glm::distance(oldCameraPosition, oldCameraTarget);
    glm::fvec3 target = camPos + forward * oldCameraDistance;

    actCam.finalCamMtx.pos = camPos;
    actCam.finalCamMtx.target = target;
    actCam.finalCamMtx.up = up;
    //actCam.finalCamMtx.up = glm::fvec3(0.0f, 1.0f, 0.0f);

    // write back the modified camera matrix to the game's memory
    uint32_t ppc_cameraMatrixOffsetOut = hCPU->gpr[31];
    writeMemory(ppc_cameraMatrixOffsetOut, &actCam);
    s_framesSinceLastCameraUpdate = 0;
}

glm::mat4 CemuHooks::s_lastCameraMtx = glm::mat4(1.0f);

void CemuHooks::hook_GetRenderCamera(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    uint32_t cameraIn = hCPU->gpr[3];
    uint32_t cameraOut = hCPU->gpr[12];
    OpenXR::EyeSide cameraSide = hCPU->gpr[11] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;

    if (CemuHooks::UseBlackBarsDuringEvents()) {
        return;
    }

    BESeadLookAtCamera camera = {};
    readMemory(cameraIn, &camera);

    Log::print<RENDERING>("Getting render camera for {} side", cameraSide == OpenXR::EyeSide::LEFT ? "left" : "right");

    //s_lastCameraMtx = glm::fmat4x3(glm::inverse(glm::mat4(camera.mtx.getLEMatrix()))); // glm::inverse(glm::lookAtRH(camera.pos.getLE(), camera.at.getLE(), camera.up.getLE()));

    // in-game camera
    glm::mat4x3 viewMatrix = camera.mtx.getLEMatrix();
    glm::mat4 worldGame = glm::inverse(glm::mat4(viewMatrix));
    glm::vec3 basePos = glm::vec3(worldGame[3]);
    glm::quat baseRot = glm::quat_cast(worldGame);

    // overwrite with our stored camera pos/rot
    basePos = s_wsCameraPosition;
    baseRot = s_wsCameraRotation;
    auto [swing, baseYaw] = swingTwistY(baseRot);

    s_lastCameraMtx = glm::fmat4x3(glm::translate(glm::identity<glm::fmat4>(), basePos) * glm::mat4(baseRot));

    // take link's direction, then rotate the headset position
    BEMatrix34 mtx = {};
    readMemory(s_playerMtxAddress, &mtx);
    glm::fvec3 playerPos = mtx.getPos().getLE();
    glm::fquat playerRot = mtx.getRotLE();

    // vr camera
    std::optional<XrPosef> currPoseOpt = VRManager::instance().XR->GetRenderer()->GetPose(cameraSide);
    if (!currPoseOpt.has_value())
        return;
    glm::fvec3 eyePos = ToGLM(currPoseOpt.value().position);
    glm::fquat eyeRot = ToGLM(currPoseOpt.value().orientation);

    if (IsFirstPerson()) {
        basePos = playerPos;
        if (auto settings = GetFirstPersonSettingsForActiveEvent()) {
            if (settings->ignoreCameraRotation) {
                baseYaw = playerRot * glm::angleAxis(glm::radians(180.0f), glm::fvec3(0.0f, 1.0f, 0.0f));
            }
        }
    }

    glm::vec3 newPos = basePos + (baseYaw * eyePos);
    glm::fquat newRot = baseYaw * eyeRot;

    glm::mat4 newWorldVR = glm::translate(glm::mat4(1.0f), newPos) * glm::mat4_cast(newRot);
    glm::mat4 newViewVR = glm::inverse(newWorldVR);

    camera.mtx.setLEMatrix(newViewVR);

    camera.pos = newPos;

    // Set look-at point by offsetting position in view direction
    glm::vec3 viewDir = -glm::vec3(newViewVR[2]); // Forward direction is -Z in view space
    camera.at = newPos + viewDir;

    // Transform world up vector by new rotation
    glm::vec3 upDir = glm::vec3(newViewVR[1]); // Up direction is +Y in view space
    camera.up = upDir;


    //glm::mat4 workingMtx = glm::inverse(glm::lookAtRH(newPos, newPos + glm::vec3(newViewVR[2]), glm::fvec3(0, 1, 0)));
    //s_lastCameraMtx = workingMtx;

    writeMemory(cameraOut, &camera);
    hCPU->gpr[3] = cameraOut;
}

constexpr uint32_t seadOrthoProjection = 0x1027B5BC;
constexpr uint32_t seadPerspectiveProjection = 0x1027B54C;


// https://github.com/KhronosGroup/OpenXR-SDK/blob/858912260ca616f4c23f7fb61c89228c353eb124/src/common/xr_linear.h#L564C1-L632C2
// https://github.com/aboood40091/sead/blob/45b629fb032d88b828600a1b787729f2d398f19d/engine/library/modules/src/gfx/seadProjection.cpp#L166

static data_VRProjectionMatrixOut calculateFOVAndOffset(XrFovf viewFOV) {
    float totalHorizontalFov = viewFOV.angleRight - viewFOV.angleLeft;
    float totalVerticalFov = viewFOV.angleUp - viewFOV.angleDown;

    float aspectRatio = totalHorizontalFov / totalVerticalFov;
    float fovY = totalVerticalFov;
    float projectionCenter_offsetX = (viewFOV.angleRight + viewFOV.angleLeft) / 2.0f;
    float projectionCenter_offsetY = (viewFOV.angleUp + viewFOV.angleDown) / 2.0f;

    data_VRProjectionMatrixOut ret = {};
    ret.aspectRatio = aspectRatio;
    ret.fovY = fovY;
    ret.offsetX = projectionCenter_offsetX;
    ret.offsetY = projectionCenter_offsetY;

    return ret;
}

static glm::mat4 calculateProjectionMatrix(float nearZ, float farZ, const XrFovf& fov) {
    float l = tanf(fov.angleLeft) * nearZ;
    float r = tanf(fov.angleRight) * nearZ;
    float b = tanf(fov.angleDown) * nearZ;
    float t = tanf(fov.angleUp) * nearZ;

    float invW = 1.0f / (r - l);
    float invH = 1.0f / (t - b);
    float invD = 1.0f / (farZ - nearZ);

    glm::mat4 dst = {};
    dst[0][0] = 2.0f * nearZ * invW;
    dst[1][1] = 2.0f * nearZ * invH;
    dst[0][2] = (r + l) * invW;
    dst[1][2] = (t + b) * invH;
    dst[2][2] = -(farZ + nearZ) * invD;
    dst[2][3] = -(2.0f * farZ * nearZ) * invD;
    dst[3][2] = -1.0f;
    dst[3][3] = 0.0f;

    return dst;
}

void CemuHooks::hook_GetRenderProjection(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    if (CemuHooks::UseBlackBarsDuringEvents()) {
        return;
    }

    uint32_t projectionIn = hCPU->gpr[3];
    uint32_t projectionOut = hCPU->gpr[12];
    OpenXR::EyeSide side = hCPU->gpr[0] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;

    BESeadPerspectiveProjection perspectiveProjection = {};
    readMemory(projectionIn, &perspectiveProjection);

    if (perspectiveProjection.zFar == 10000.0f) {
        return;
    }

    perspectiveProjection.zFar = GetSettings().GetZFar();
    perspectiveProjection.zNear = GetSettings().GetZNear();

    if (!VRManager::instance().XR->GetRenderer()->GetFOV(side).has_value()) {
        return;
    }
    XrFovf currFOV = VRManager::instance().XR->GetRenderer()->GetFOV(side).value();
    auto newProjection = calculateFOVAndOffset(currFOV);

    perspectiveProjection.aspect = newProjection.aspectRatio;
    perspectiveProjection.fovYRadiansOrAngle = newProjection.fovY;
    float halfAngle = newProjection.fovY.getLE() * 0.5f;
    perspectiveProjection.fovySin = sinf(halfAngle);
    perspectiveProjection.fovyCos = cosf(halfAngle);
    perspectiveProjection.fovyTan = tanf(halfAngle);
    perspectiveProjection.offset.x = newProjection.offsetX;
    perspectiveProjection.offset.y = newProjection.offsetY;

    glm::fmat4 newMatrix = calculateProjectionMatrix(perspectiveProjection.zNear.getLE(), perspectiveProjection.zFar.getLE(), currFOV);
    perspectiveProjection.matrix = newMatrix;

    // calculate device matrix
    glm::fmat4 newDeviceMatrix = newMatrix;

    float zScale = perspectiveProjection.deviceZScale.getLE();
    float zOffset = perspectiveProjection.deviceZOffset.getLE();

    newDeviceMatrix[2][0] *= zScale;
    newDeviceMatrix[2][1] *= zScale;
    newDeviceMatrix[2][2] = (newDeviceMatrix[2][2] + newDeviceMatrix[3][2] * zOffset) * zScale;
    newDeviceMatrix[2][3] = newDeviceMatrix[2][3] * zScale + newDeviceMatrix[3][3] * zOffset;

    perspectiveProjection.deviceMatrix = newDeviceMatrix;

    perspectiveProjection.dirty = false;
    perspectiveProjection.deviceDirty = false;

    writeMemory(projectionOut, &perspectiveProjection);
    hCPU->gpr[3] = projectionOut;
}


void CemuHooks::hook_ModifyLightPrePassProjectionMatrix(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    if (CemuHooks::UseBlackBarsDuringEvents()) {
        return;
    }

    uint32_t projectionIn = hCPU->gpr[3];
    OpenXR::EyeSide side = hCPU->gpr[11] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;

    BESeadPerspectiveProjection perspectiveProjection = {};
    readMemory(projectionIn, &perspectiveProjection);

    if (!VRManager::instance().XR->GetRenderer()->GetFOV(side).has_value()) {
        return;
    }

    XrFovf currFOV = VRManager::instance().XR->GetRenderer()->GetFOV(side).value();
    auto newProjection = calculateFOVAndOffset(currFOV);

    perspectiveProjection.aspect = newProjection.aspectRatio;
    perspectiveProjection.fovYRadiansOrAngle = newProjection.fovY;
    float halfAngle = newProjection.fovY.getLE() * 0.5f;
    perspectiveProjection.fovySin = sinf(halfAngle);
    perspectiveProjection.fovyCos = cosf(halfAngle);
    perspectiveProjection.fovyTan = tanf(halfAngle);
    perspectiveProjection.offset.x = newProjection.offsetX;
    perspectiveProjection.offset.y = newProjection.offsetY;

    glm::fmat4 newMatrix = calculateProjectionMatrix(perspectiveProjection.zNear.getLE(), perspectiveProjection.zFar.getLE(), currFOV);
    perspectiveProjection.matrix = newMatrix;

    // calculate device matrix
    glm::fmat4 newDeviceMatrix = newMatrix;

    float zScale = perspectiveProjection.deviceZScale.getLE();
    float zOffset = perspectiveProjection.deviceZOffset.getLE();

    newDeviceMatrix[2][0] *= zScale;
    newDeviceMatrix[2][1] *= zScale;
    newDeviceMatrix[2][2] = (newDeviceMatrix[2][2] + newDeviceMatrix[3][2] * zOffset) * zScale;
    newDeviceMatrix[2][3] = newDeviceMatrix[2][3] * zScale + newDeviceMatrix[3][3] * zOffset;

    perspectiveProjection.deviceMatrix = newDeviceMatrix;

    perspectiveProjection.dirty = false;
    perspectiveProjection.deviceDirty = false;

    writeMemory(projectionIn, &perspectiveProjection);
}

void CemuHooks::hook_EndCameraSide(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    OpenXR::EyeSide side = hCPU->gpr[3] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;

    // todo: sometimes this can deadlock apparently?
    if (VRManager::instance().XR->GetRenderer()->IsInitialized() && side == OpenXR::EyeSide::RIGHT) {
        VRManager::instance().XR->GetRenderer()->EndFrame();
        CemuHooks::m_heldWeaponsLastUpdate[0] = CemuHooks::m_heldWeaponsLastUpdate[0]++;
        CemuHooks::m_heldWeaponsLastUpdate[1] = CemuHooks::m_heldWeaponsLastUpdate[1]++;
        if (CemuHooks::m_heldWeaponsLastUpdate[0] >= 6) {
            CemuHooks::m_heldWeapons[0] = 0;
        }
        if (CemuHooks::m_heldWeaponsLastUpdate[1] >= 6) {
            CemuHooks::m_heldWeapons[1] = 0;
        }
    }

    Log::print<RENDERING>("{0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0}", side == OpenXR::EyeSide::LEFT ? "LEFT" : "RIGHT");
    Log::print<RENDERING>("===============================================================================");
    Log::print<RENDERING>("");
}

void CemuHooks::hook_ReplaceCameraMode(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    uint32_t currentCameraMode = hCPU->gpr[3];
    uint32_t cameraTailMode = hCPU->gpr[4]; // works best in VR since it ignores the pivot of the camera
    uint32_t currentCameraVtbl = hCPU->gpr[5];

    constexpr uint32_t kCameraChaseVtbl = 0x101B34F4;

    if (hCPU->gpr[5] == kCameraChaseVtbl) {
        //Log::print("Current camera mode: {:#X}, tail mode: {:#X}, vtbl: {:#X}", currentCameraMode, cameraTailMode, currentCameraVtbl);
        if (IsFirstPerson()) {
            // overwrite to tail mode
            //hCPU->gpr[3] = cameraTailMode;
        }
    }

    //Log::print<VERBOSE>("Camera mode: {:#X}, tail mode: {:#X}, vtbl: {:#X}", currentCameraMode, cameraTailMode, currentCameraVtbl);
}

void CemuHooks::hook_UseCameraDistance(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    if (IsFirstPerson()) {
        hCPU->fpr[13].fp0 = 0.0f;
    }
    else {
        hCPU->fpr[13].fp0 = hCPU->fpr[10].fp0;
    }
}

void CemuHooks::hook_SetActorOpacity(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    double toBeSetOpacity = hCPU->fpr[1].fp0;
    uint32_t actorPtr = hCPU->gpr[3];

    ActorWiiU actor;
    readMemory(actorPtr, &actor);

    // normal behavior if it wasn't the player or a held weapon
    if (actor.modelOpacity.getLE() != toBeSetOpacity) {
        uint8_t opacityOrDoFlushOpacityToGPU = 1;
        writeMemoryBE(actorPtr + offsetof(ActorWiiU, modelOpacity), &toBeSetOpacity);
        writeMemoryBE(actorPtr + offsetof(ActorWiiU, opacityOrDoFlushOpacityToGPU), &opacityOrDoFlushOpacityToGPU);
    }
}

void CemuHooks::hook_CalculateModelOpacity(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    
    // overwrites return value to 1 if in first person mode
    if (IsFirstPerson()) {
        hCPU->fpr[1].fp0 = 1.0f;
    }
}

std::string CemuHooks::s_currentEvent = {};
CemuHooks::HybridEventSettings CemuHooks::s_currentEventSettings = {};
std::unordered_map<std::string, CemuHooks::HybridEventSettings> CemuHooks::s_eventSettings = {};

constexpr CemuHooks::HybridEventSettings defaultFirstPersonSettings = {
    .firstPerson = true,
    .disablePlayerDrivenLinkHands = false,
    .ignoreCameraRotation = true
};

void CemuHooks::initCutsceneDefaultSettings(uint32_t ppc_TableOfCutsceneEventsSettingsOffset) {
    if (!s_eventSettings.empty()) {
        return;
    }

    char* currPtr = reinterpret_cast<char*>(s_memoryBaseAddress + ppc_TableOfCutsceneEventsSettingsOffset);
    while (true) {
        const std::string line(currPtr);
        if (line.empty()) {
            break;
        }
        size_t commaPos = line.find(',');
        if (commaPos != std::string::npos) {
            HybridEventSettings entry = {};
            std::string settingsStr = line.substr(commaPos + 1);
            std::string eventName = line.substr(0, commaPos);
            size_t pos = 0;
            while ((pos = settingsStr.find(',')) != std::string::npos) {
                std::string setting = settingsStr.substr(0, pos);
                if (setting == "FP_ON") entry.firstPerson = true;
                else if (setting == "FP_OFF") entry.firstPerson = false;
                else if (setting == "HND_ON") entry.disablePlayerDrivenLinkHands = false;
                else if (setting == "HND_OFF") entry.disablePlayerDrivenLinkHands = true;
                else if (setting == "PAN_ON") entry.ignoreCameraRotation = false;
                else if (setting == "PAN_OFF") entry.ignoreCameraRotation = true;
                else if (setting == "CTRL_ON") entry.demoEnableCameraInput = false;
                else if (setting == "CTRL_OFF") entry.demoEnableCameraInput = true;
                else {
                    Log::print<WARNING>("Unknown cutscene default setting: {}", setting);
                }
                settingsStr.erase(0, pos + 1);
            }
            // last setting
            std::string setting = settingsStr;
            if (setting == "FP_ON") entry.firstPerson = true;
            else if (setting == "FP_OFF") entry.firstPerson = false;
            else if (setting == "HND_ON") entry.disablePlayerDrivenLinkHands = false;
            else if (setting == "HND_OFF") entry.disablePlayerDrivenLinkHands = true;
            else if (setting == "PAN_ON") entry.ignoreCameraRotation = false;
            else if (setting == "PAN_OFF") entry.ignoreCameraRotation = true;
            else if (setting == "CTRL_ON") entry.demoEnableCameraInput = false;
            else if (setting == "CTRL_OFF") entry.demoEnableCameraInput = true;
            else {
                Log::print<WARNING>("Unknown cutscene default setting: {}", setting);
            }
            s_eventSettings.insert_or_assign(eventName, entry);
        }
        currPtr += line.length() + 1;
    }

    Log::print<VERBOSE>("Initialized cutscene default settings for {} events.", s_eventSettings.size());
}

void CemuHooks::hook_GetEventName(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    uint32_t isEventActive = hCPU->gpr[3];
    if (isEventActive) {
        const char* eventNamePtr = (const char*)(hCPU->gpr[4] + s_memoryBaseAddress);
        std::string eventName = std::string(eventNamePtr);
        if (s_currentEvent == eventName) {
            return;
        }
        Log::print<INFO>("Event '{}' is now active.", eventName);
        s_currentEvent = eventName;

        auto it = s_eventSettings.find(eventName);
        if (it != s_eventSettings.end()) {
            HybridEventSettings settings = it->second;
            Log::print<VERBOSE>(" - First Person: {}", settings.firstPerson ? "ON" : "OFF");
            Log::print<VERBOSE>(" - Ignore Camera Rotation: {}", settings.ignoreCameraRotation ? "ON" : "OFF");
            Log::print<VERBOSE>(" - Disable Player-Driven Link Hands: {}", settings.disablePlayerDrivenLinkHands ? "ON" : "OFF");
            s_currentEventSettings = settings;
        }
        else {
            Log::print<VERBOSE>(" - No specific settings found for this event, using defaults.");
            s_currentEventSettings = defaultFirstPersonSettings;
        }

        // In cutscene's there's somethings a mention of Demo_EnableCameraInput/Demo_EnableCameraControlByUser/Demo_DisableCameraInput
        // These don't actually seem to be hooked up. There is a flag called 

        // IF THERE'S AN EVENT + Link's camera controls are disabled, we should check the type of event and then disable the camera accordingly to the categorized type
        // shrine going down is Demo008_2
        // camera zoom when opening door is Demo024_0
    }
    else if (!s_currentEvent.empty()) {
        //Log::print("!! There's no active event");
        s_currentEvent = "";
    }
}

constexpr uint32_t orig_GetStaticParam_float_funcAddr = 0x030E9BE0;

void CemuHooks::hook_OverwriteCameraParam(PPCInterpreter_t* hCPU) {
    if (GetSettings().IsFirstPersonMode()) {
        hCPU->instructionPointer = hCPU->sprNew.LR;

        uint32_t destFloatPtr = getMemory<BEType<uint32_t>>(hCPU->gpr[4]).getLE();
        uint32_t superLowAddress = 0x102B3150; // points to 0.0000011920929
        writeMemoryBE(hCPU->gpr[4], &superLowAddress);
    }
    else {
        hCPU->instructionPointer = orig_GetStaticParam_float_funcAddr;
    }
}
