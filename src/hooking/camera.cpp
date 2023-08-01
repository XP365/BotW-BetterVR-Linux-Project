#include "cemu_hooks.h"
#include "instance.h"
#include "rendering/openxr.h"

#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/projection.hpp>
#include <glm/gtx/quaternion.hpp>

data_VRProjectionMatrixOut calculateProjectionMatrix(XrFovf viewFOV) {
    float totalHorizontalFov = viewFOV.angleRight - viewFOV.angleLeft;
    float totalVerticalFov = viewFOV.angleUp - viewFOV.angleDown;

    float aspectRatio = totalHorizontalFov / totalVerticalFov;
    float fovY = totalVerticalFov;
    float projectionCenter_offsetX = (viewFOV.angleRight + viewFOV.angleLeft) / 2.0f;
    float projectionCenter_offsetY = (viewFOV.angleUp + viewFOV.angleDown) / 2.0f;

    return {
        .aspectRatio = aspectRatio,
        .fovY = fovY,
        .offsetX = projectionCenter_offsetX,
        .offsetY = projectionCenter_offsetY,
    };
}

// todo: for non-EAR versions it should use the same camera inputs for both eyes
void CemuHooks::hook_UpdateCamera(PPCInterpreter_t* hCPU) {
    //Log::print("Updated camera!");
    hCPU->instructionPointer = hCPU->gpr[7];

    // Read the camera matrix from the game's memory
    uint32_t ppc_cameraMatrixOffsetIn = hCPU->gpr[30];
    data_VRCameraIn origCameraMatrix = {};

    readMemory(ppc_cameraMatrixOffsetIn, &origCameraMatrix);
    swapEndianness(origCameraMatrix.posX);
    swapEndianness(origCameraMatrix.posY);
    swapEndianness(origCameraMatrix.posZ);
    swapEndianness(origCameraMatrix.targetX);
    swapEndianness(origCameraMatrix.targetY);
    swapEndianness(origCameraMatrix.targetZ);

    data_VRSettingsIn settings = VRManager::instance().Hooks->GetSettings();

    // Current VR headset camera matrix
    XrPosef currPose = VRManager::instance().XR->GetRenderer()->m_layer3D.GetCurrentPose();
    XrFovf currFov = VRManager::instance().XR->GetRenderer()->m_layer3D.GetCurrentFOV();
    auto currProjectionMatrix = calculateProjectionMatrix(currFov);

    glm::fvec3 currEyePos(currPose.position.x, currPose.position.y, currPose.position.z);
    glm::fquat currEyeQuat(currPose.orientation.w, currPose.orientation.x, currPose.orientation.y, currPose.orientation.z);
    //Log::print("Headset View: x={}, y={}, z={}, orientW={}, orientX={}, orientY={}, orientZ={}", currEyePos.x, currEyePos.y, currEyePos.z, currEyeQuat.w, currEyeQuat.x, currEyeQuat.y, currEyeQuat.z);

    // Current in-game camera matrix
    glm::fvec3 oldCameraPosition(origCameraMatrix.posX, origCameraMatrix.posY, origCameraMatrix.posZ);
    glm::fvec3 oldCameraTarget(origCameraMatrix.targetX, origCameraMatrix.targetY, origCameraMatrix.targetZ);
    float oldCameraDistance = glm::distance(oldCameraPosition, oldCameraTarget);
    //Log::print("Original Game Camera: x={}, y={}, z={}, targetX={}, targetY={}, targetZ={}", oldCameraPosition.x, oldCameraPosition.y, oldCameraPosition.z, oldCameraTarget.x, oldCameraTarget.y, oldCameraTarget.z);

    // Calculate game view directions
    glm::fvec3 forwardVector = glm::normalize(oldCameraTarget - oldCameraPosition);
    glm::fquat lookAtQuat = glm::quatLookAtRH(forwardVector, { 0.0, 1.0, 0.0 });

    // Calculate new view direction
    glm::fquat combinedQuat = glm::normalize(lookAtQuat * currEyeQuat);
    glm::fmat3 combinedMatrix = glm::toMat3(combinedQuat);

    // Rotate the headset position by the in-game rotation
    glm::fvec3 rotatedHmdPos = lookAtQuat * currEyePos;

    data_VRCameraOut updatedCameraMatrix = {
        .posX = oldCameraPosition.x + rotatedHmdPos.x,
        .posY = oldCameraPosition.y + rotatedHmdPos.y,
        .posZ = oldCameraPosition.z + rotatedHmdPos.z,
        // pos + rotated headset pos + inverted forward direction after combining both the in-game and HMD rotation
        .targetX = oldCameraPosition.x + rotatedHmdPos.x + ((combinedMatrix[2][0] * -1.0f) * oldCameraDistance),
        .targetY = oldCameraPosition.y + rotatedHmdPos.y + ((combinedMatrix[2][1] * -1.0f) * oldCameraDistance),
        .targetZ = oldCameraPosition.z + rotatedHmdPos.z + ((combinedMatrix[2][2] * -1.0f) * oldCameraDistance),
        .rotX = combinedMatrix[1][0],
        .rotY = combinedMatrix[1][1],
        .rotZ = combinedMatrix[1][2],
        .aspectRatio = currProjectionMatrix.aspectRatio,
        .fovY = currProjectionMatrix.fovY,
    };
    Log::print("[{}] Updated camera position", VRManager::instance().XR->GetRenderer()->m_layer3D.GetCurrentSide() == OpenXR::EyeSide::LEFT ? "left" : "right");

    // Write the camera matrix to the game's memory
    // Log::print("[{}] New Game Camera: x={}, y={}, z={}, targetX={}, targetY={}, targetZ={}, rotX={}, rotY={}, rotZ={}", VRManager::instance().XR->GetRenderer()->m_layer3D.GetCurrentSide() == OpenXR::EyeSide::LEFT ? "left" : "right", updatedCameraMatrix.posX, updatedCameraMatrix.posY, updatedCameraMatrix.posZ, updatedCameraMatrix.targetX, updatedCameraMatrix.targetY, updatedCameraMatrix.targetZ, updatedCameraMatrix.rotX, updatedCameraMatrix.rotY, updatedCameraMatrix.rotZ);
    swapEndianness(updatedCameraMatrix.posX);
    swapEndianness(updatedCameraMatrix.posY);
    swapEndianness(updatedCameraMatrix.posZ);
    swapEndianness(updatedCameraMatrix.targetX);
    swapEndianness(updatedCameraMatrix.targetY);
    swapEndianness(updatedCameraMatrix.targetZ);
    swapEndianness(updatedCameraMatrix.rotX);
    swapEndianness(updatedCameraMatrix.rotY);
    swapEndianness(updatedCameraMatrix.rotZ);
    swapEndianness(updatedCameraMatrix.aspectRatio);
    swapEndianness(updatedCameraMatrix.fovY);
    uint32_t ppc_cameraMatrixOffsetOut = hCPU->gpr[31];
    writeMemory(ppc_cameraMatrixOffsetOut, &updatedCameraMatrix);
}

void CemuHooks::hook_UpdateCameraRotation(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    Log::print("[{}] Updated camera rotation", VRManager::instance().XR->GetRenderer()->m_layer3D.GetCurrentSide() == OpenXR::EyeSide::LEFT ? "left" : "right");
}


uint32_t counter = -1;
OpenXR::EyeSide matrixSide = OpenXR::EyeSide::LEFT;

void CemuHooks::hook_UpdateCameraOffset(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    Log::print("[{}] Updated camera FOV and projection offset", matrixSide == OpenXR::EyeSide::LEFT ? "left" : "right");

    XrFovf viewFOV = VRManager::instance().XR->GetRenderer()->m_layer3D.GetFOV(matrixSide);
    checkAssert(viewFOV.angleLeft <= viewFOV.angleRight, "OpenXR gave a left FOV that is larger than the right FOV! Behavior is unexpected!");
    checkAssert(viewFOV.angleDown <= viewFOV.angleUp, "OpenXR gave a top FOV that is larger than the bottom FOV! Behavior is unexpected!");

    data_VRProjectionMatrixOut projectionMatrix = calculateProjectionMatrix(viewFOV);

    data_VRCameraOffsetOut cameraOffsetOut = {
        .aspectRatio = projectionMatrix.aspectRatio,
        .fovY = projectionMatrix.fovY,
        .offsetX = projectionMatrix.offsetX,
        .offsetY = projectionMatrix.offsetY
    };
    swapEndianness(cameraOffsetOut.aspectRatio);
    swapEndianness(cameraOffsetOut.fovY);
    swapEndianness(cameraOffsetOut.offsetX);
    swapEndianness(cameraOffsetOut.offsetY);
    uint32_t ppc_projectionMatrixOut = hCPU->gpr[11];
    writeMemory(ppc_projectionMatrixOut, &cameraOffsetOut);
}


void CemuHooks::hook_CalculateCameraAspectRatio(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    Log::print("[{}] Updated camera aspect ratio", matrixSide == OpenXR::EyeSide::LEFT ? "left" : "right");

    // fixme: this is a very, very hacky workaround that seems to work well until it desyncs
    // howtofix: find out how to associate the projection matrix to a single camera frame. Currently it gets called multiple times (seemingly twice? per frame), but it gets called from outside of the rendering thread. It's difficult to
    counter++;
    if (counter >= 2) {
        matrixSide = matrixSide == OpenXR::EyeSide::LEFT ? OpenXR::EyeSide::RIGHT : OpenXR::EyeSide::LEFT;
        counter = 0;
    }

    XrFovf viewFOV = VRManager::instance().XR->GetRenderer()->m_layer3D.GetFOV(matrixSide);
    checkAssert(viewFOV.angleLeft <= viewFOV.angleRight, "OpenXR gave a left FOV that is larger than the right FOV! Behavior is unexpected!");
    checkAssert(viewFOV.angleDown <= viewFOV.angleUp, "OpenXR gave a top FOV that is larger than the bottom FOV! Behavior is unexpected!");

    data_VRProjectionMatrixOut projectionMatrix = calculateProjectionMatrix(viewFOV);

    data_VRCameraAspectRatioOut cameraOffsetOut = {
        .aspectRatio = projectionMatrix.aspectRatio,
        .fovY = projectionMatrix.fovY
    };
    swapEndianness(cameraOffsetOut.aspectRatio);
    swapEndianness(cameraOffsetOut.fovY);
    uint32_t ppc_projectionMatrixOut = hCPU->gpr[28];
    writeMemory(ppc_projectionMatrixOut, &cameraOffsetOut);
}



//void CemuHooks::hook_UpdateProjectionMatrix(PPCInterpreter_t* hCPU) {
//    hCPU->instructionPointer = hCPU->sprNew.LR;
//
//    Log::print("[{}] Updating projection matrix for camera at {:08X}", VRManager::instance().XR->GetRenderer()->m_layer3D.GetCurrentSide() == OpenXR::EyeSide::LEFT ? "left" : "right", hCPU->gpr[3]);
//
//    XrFovf viewFOV = VRManager::instance().XR->GetRenderer()->m_layer3D.GetCurrentFOV();
//    checkAssert(viewFOV.angleLeft <= viewFOV.angleRight, "OpenXR gave a left FOV that is larger than the right FOV! Behavior is unexpected!");
//    checkAssert(viewFOV.angleDown <= viewFOV.angleUp, "OpenXR gave a top FOV that is larger than the bottom FOV! Behavior is unexpected!");
//
//    data_VRProjectionMatrixOut projectionMatrixOut = calculateProjectionMatrix(viewFOV);
//    swapEndianness(projectionMatrixOut.aspectRatio);
//    swapEndianness(projectionMatrixOut.fovY);
//    swapEndianness(projectionMatrixOut.offsetX);
//    swapEndianness(projectionMatrixOut.offsetY);
//    uint32_t ppc_projectionMatrixOut = hCPU->gpr[28];
//    writeMemory(ppc_projectionMatrixOut, &projectionMatrixOut);
//}