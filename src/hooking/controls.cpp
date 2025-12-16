#include "cemu_hooks.h"
#include "../instance.h"

enum VPADButtons : uint32_t {
    VPAD_BUTTON_A                 = 0x8000,
    VPAD_BUTTON_B                 = 0x4000,
    VPAD_BUTTON_X                 = 0x2000,
    VPAD_BUTTON_Y                 = 0x1000,
    VPAD_BUTTON_LEFT              = 0x0800,
    VPAD_BUTTON_RIGHT             = 0x0400,
    VPAD_BUTTON_UP                = 0x0200,
    VPAD_BUTTON_DOWN              = 0x0100,
    VPAD_BUTTON_ZL                = 0x0080,
    VPAD_BUTTON_ZR                = 0x0040,
    VPAD_BUTTON_L                 = 0x0020,
    VPAD_BUTTON_R                 = 0x0010,
    VPAD_BUTTON_PLUS              = 0x0008,
    VPAD_BUTTON_MINUS             = 0x0004,
    VPAD_BUTTON_HOME              = 0x0002,
    VPAD_BUTTON_SYNC              = 0x0001,
    VPAD_BUTTON_STICK_R           = 0x00020000,
    VPAD_BUTTON_STICK_L           = 0x00040000,
    VPAD_BUTTON_TV                = 0x00010000,
    VPAD_STICK_R_EMULATION_LEFT   = 0x04000000,
    VPAD_STICK_R_EMULATION_RIGHT  = 0x02000000,
    VPAD_STICK_R_EMULATION_UP     = 0x01000000,
    VPAD_STICK_R_EMULATION_DOWN   = 0x00800000,
    VPAD_STICK_L_EMULATION_LEFT   = 0x40000000,
    VPAD_STICK_L_EMULATION_RIGHT  = 0x20000000,
    VPAD_STICK_L_EMULATION_UP     = 0x10000000,
    VPAD_STICK_L_EMULATION_DOWN   = 0x08000000,
};

struct BEDir {
    BEVec3 x;
    BEVec3 y;
    BEVec3 z;

    BEDir() = default;
};

struct BETouchData {
    BEType<uint16_t> x;
    BEType<uint16_t> y;
    BEType<uint16_t> touch;
    BEType<uint16_t> validity;
};

struct VPADStatus {
    BEType<uint32_t> hold;
    BEType<uint32_t> trig;
    BEType<uint32_t> release;
    BEVec2 leftStick;
    BEVec2 rightStick;
    BEVec3 acc;
    BEType<float> accMagnitude;
    BEType<float> accAcceleration;
    BEVec2 accXY;
    BEVec3 gyroChange;
    BEVec3 gyroOrientation;
    int8_t vpadErr;
    uint8_t padding1[1];
    BETouchData tpData;
    BETouchData tpProcessed1;
    BETouchData tpProcessed2;
    uint8_t padding2[2];
    BEDir dir;
    uint8_t headphoneStatus;
    uint8_t padding3[3];
    BEVec3 magnet;
    uint8_t slideVolume;
    uint8_t batteryLevel;
    uint8_t micStatus;
    uint8_t slideVolume2;
    uint8_t padding4[8];
};
static_assert(sizeof(VPADStatus) == 0xAC);


void CemuHooks::hook_InjectXRInput(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    auto mapXRButtonToVpad = [](XrActionStateBoolean& state, VPADButtons mapping) -> uint32_t {
        return state.currentState ? mapping : 0;
    };

    // read existing vpad as to not overwrite it
    uint32_t vpadStatusOffset = hCPU->gpr[4];
    VPADStatus vpadStatus = {};
    // todo: revert this to unblock gamepad input
    if (!(VRManager::instance().VK->m_imguiOverlay && VRManager::instance().VK->m_imguiOverlay->ShouldBlockGameInput())) {
        readMemory(vpadStatusOffset, &vpadStatus);
    }

    OpenXR::InputState inputs = VRManager::instance().XR->m_input.load();

    // buttons
    static uint32_t oldCombinedHold = 0;
    uint32_t newXRBtnHold = 0;

    // we need the stick inputs for some bindings
    XrActionStateVector2f& leftStickSource = inputs.inGame.in_game ? inputs.inGame.move : inputs.inMenu.navigate;

    if (inputs.inGame.in_game) {
        newXRBtnHold |= mapXRButtonToVpad(inputs.inGame.map, VPAD_BUTTON_MINUS);
        newXRBtnHold |= mapXRButtonToVpad(inputs.inGame.inventory, VPAD_BUTTON_PLUS);

        newXRBtnHold |= mapXRButtonToVpad(inputs.inGame.jump, VPAD_BUTTON_X);
        newXRBtnHold |= mapXRButtonToVpad(inputs.inGame.run, VPAD_BUTTON_B);

        newXRBtnHold |= mapXRButtonToVpad(inputs.inGame.cancel, VPAD_BUTTON_B);

        newXRBtnHold |= mapXRButtonToVpad(inputs.inGame.interact, VPAD_BUTTON_A);

        if (inputs.inGame.grabState[0].lastEvent == GrabButtonState::Event::ShortPress)
            newXRBtnHold |= VPAD_BUTTON_L;

        if (inputs.inGame.grabState[1].lastEvent == GrabButtonState::Event::ShortPress)
            newXRBtnHold |= VPAD_BUTTON_R;

        if (inputs.inGame.grabState[0].wasDownLastFrame)
        {
            if (leftStickSource.currentState.y >= 0.5f) {
                newXRBtnHold |= VPAD_BUTTON_UP;
            }
            else if (leftStickSource.currentState.y <= -0.5f) {
                newXRBtnHold |= VPAD_BUTTON_DOWN;
            }

            if (leftStickSource.currentState.x <= -0.5f) {
                newXRBtnHold |= VPAD_BUTTON_LEFT;
            }

            else if (leftStickSource.currentState.x >= 0.5f) {
                newXRBtnHold |= VPAD_BUTTON_RIGHT;
            }
        }
        
        newXRBtnHold |= mapXRButtonToVpad(inputs.inGame.leftTrigger, VPAD_BUTTON_ZL);
        newXRBtnHold |= mapXRButtonToVpad(inputs.inGame.rightTrigger, VPAD_BUTTON_ZR);

        //newXRBtnHold |= mapXRButtonToVpad(inputs.inGame.grab[0], VPAD_BUTTON_A);
        //newXRBtnHold |= mapXRButtonToVpad(inputs.inGame.grab[1], VPAD_BUTTON_A);
    }
    else {
        newXRBtnHold |= mapXRButtonToVpad(inputs.inMenu.map, VPAD_BUTTON_MINUS);
        newXRBtnHold |= mapXRButtonToVpad(inputs.inMenu.inventory, VPAD_BUTTON_PLUS);

        newXRBtnHold |= mapXRButtonToVpad(inputs.inMenu.select, VPAD_BUTTON_A);
        newXRBtnHold |= mapXRButtonToVpad(inputs.inMenu.back, VPAD_BUTTON_B);
        newXRBtnHold |= mapXRButtonToVpad(inputs.inMenu.sort, VPAD_BUTTON_Y);
        newXRBtnHold |= mapXRButtonToVpad(inputs.inMenu.hold, VPAD_BUTTON_X);

        newXRBtnHold |= mapXRButtonToVpad(inputs.inMenu.leftGrip, VPAD_BUTTON_L);
        newXRBtnHold |= mapXRButtonToVpad(inputs.inMenu.rightGrip, VPAD_BUTTON_R);

        if (inputs.inMenu.leftGrip.currentState)
        {
            if (leftStickSource.currentState.y >= 0.5f) {
                newXRBtnHold |= VPAD_BUTTON_UP;
            }
            else if (leftStickSource.currentState.y <= -0.5f) {
                newXRBtnHold |= VPAD_BUTTON_DOWN;
            }

            if (leftStickSource.currentState.x <= -0.5f) {
                newXRBtnHold |= VPAD_BUTTON_LEFT;
            }

            else if (leftStickSource.currentState.x >= 0.5f) {
                newXRBtnHold |= VPAD_BUTTON_RIGHT;
            }
        }
    }

    // todo: see if select or grab is better

    // sticks
    static uint32_t oldXRStickHold = 0;
    uint32_t newXRStickHold = 0;

    constexpr float AXIS_THRESHOLD = 0.5f;
    constexpr float HOLD_THRESHOLD = 0.1f;

    // movement/navigation stick
    leftStickSource = inputs.inGame.in_game ? inputs.inGame.move : inputs.inMenu.navigate;

    auto input = VRManager::instance().XR->m_input.load();

    if (input.inGame.in_game) {
        auto isolateYaw = [](const glm::fquat& q) -> glm::fquat {
            glm::vec3 euler = glm::eulerAngles(q);
            euler.x = 0.0f;
            euler.z = 0.0f;
            return glm::angleAxis(euler.y, glm::vec3(0, 1, 0));
        };

        glm::fquat controllerRotation = ToGLM(input.inGame.poseLocation[OpenXR::EyeSide::LEFT].pose.orientation);
        glm::fquat controllerYawRotation = isolateYaw(controllerRotation);

        glm::fquat moveRotation = input.inGame.pose[OpenXR::EyeSide::LEFT].isActive ? glm::inverse(VRManager::instance().XR->m_inputCameraRotation.load() * controllerYawRotation) : glm::identity<glm::fquat>();

        glm::vec3 localMoveVec(leftStickSource.currentState.x, 0.0f, leftStickSource.currentState.y);

        glm::vec3 worldMoveVec = moveRotation * localMoveVec;

        float inputLen = glm::length(glm::vec2(leftStickSource.currentState.x, leftStickSource.currentState.y));
        if (inputLen > 1e-3f) {
            worldMoveVec = glm::normalize(worldMoveVec) * inputLen;
        }
        else {
            worldMoveVec = glm::vec3(0.0f);
        }

        worldMoveVec = {0, 0, 0};

        vpadStatus.leftStick = { worldMoveVec.x + leftStickSource.currentState.x + vpadStatus.leftStick.x.getLE(), worldMoveVec.z + leftStickSource.currentState.y + vpadStatus.leftStick.y.getLE() };
    }
    else {
        vpadStatus.leftStick = { leftStickSource.currentState.x + vpadStatus.leftStick.x.getLE(), leftStickSource.currentState.y + vpadStatus.leftStick.y.getLE() };
    }

    if (leftStickSource.currentState.x <= -AXIS_THRESHOLD || (HAS_FLAG(oldXRStickHold, VPAD_STICK_L_EMULATION_LEFT) && leftStickSource.currentState.x <= -HOLD_THRESHOLD))
        newXRStickHold |= VPAD_STICK_L_EMULATION_LEFT;
    else if (leftStickSource.currentState.x >= AXIS_THRESHOLD || (HAS_FLAG(oldXRStickHold, VPAD_STICK_L_EMULATION_RIGHT) && leftStickSource.currentState.x >= HOLD_THRESHOLD))
        newXRStickHold |= VPAD_STICK_L_EMULATION_RIGHT;

    if (leftStickSource.currentState.y <= -AXIS_THRESHOLD || (HAS_FLAG(oldXRStickHold, VPAD_STICK_L_EMULATION_DOWN) && leftStickSource.currentState.y <= -HOLD_THRESHOLD))
        newXRStickHold |= VPAD_STICK_L_EMULATION_DOWN;
    else if (leftStickSource.currentState.y >= AXIS_THRESHOLD || (HAS_FLAG(oldXRStickHold, VPAD_STICK_L_EMULATION_UP) && leftStickSource.currentState.y >= HOLD_THRESHOLD))
        newXRStickHold |= VPAD_STICK_L_EMULATION_UP;


    // camera/fast-scroll stick
    XrActionStateVector2f& rightStickSource = inputs.inGame.in_game ? inputs.inGame.camera : inputs.inMenu.scroll;

    // disable up-and-down tilting
    if (IsFirstPerson() && inputs.inGame.in_game) {
        rightStickSource.currentState.y = 0;
    }

    vpadStatus.rightStick = {rightStickSource.currentState.x + vpadStatus.rightStick.x.getLE(), rightStickSource.currentState.y + vpadStatus.rightStick.y.getLE()};

    if (rightStickSource.currentState.x <= -AXIS_THRESHOLD || (HAS_FLAG(oldXRStickHold, VPAD_STICK_R_EMULATION_LEFT) && rightStickSource.currentState.x <= -HOLD_THRESHOLD))
        newXRStickHold |= VPAD_STICK_R_EMULATION_LEFT;
    else if (rightStickSource.currentState.x >= AXIS_THRESHOLD || (HAS_FLAG(oldXRStickHold, VPAD_STICK_R_EMULATION_RIGHT) && rightStickSource.currentState.x >= HOLD_THRESHOLD))
        newXRStickHold |= VPAD_STICK_R_EMULATION_RIGHT;

    if (rightStickSource.currentState.y <= -AXIS_THRESHOLD || (HAS_FLAG(oldXRStickHold, VPAD_STICK_R_EMULATION_DOWN) && rightStickSource.currentState.y <= -HOLD_THRESHOLD))
        newXRStickHold |= VPAD_STICK_R_EMULATION_DOWN;
    else if (rightStickSource.currentState.y >= AXIS_THRESHOLD || (HAS_FLAG(oldXRStickHold, VPAD_STICK_R_EMULATION_UP) && rightStickSource.currentState.y >= HOLD_THRESHOLD))
        newXRStickHold |= VPAD_STICK_R_EMULATION_UP;

    oldXRStickHold = newXRStickHold;

    // calculate new hold, trigger and release
    uint32_t combinedHold = (vpadStatus.hold.getLE() | (newXRBtnHold | newXRStickHold));
    vpadStatus.hold = combinedHold;
    vpadStatus.trig = (combinedHold & ~oldCombinedHold);
    vpadStatus.release = (~combinedHold & oldCombinedHold);
    oldCombinedHold = combinedHold;

    // misc
    vpadStatus.vpadErr = 0;
    vpadStatus.batteryLevel = 0xC0;

    // touch
    vpadStatus.tpData.touch = 0;
    vpadStatus.tpData.validity = 3;

    // motion
    vpadStatus.dir.x = glm::fvec3{1, 0, 0};
    vpadStatus.dir.y = glm::fvec3{0, 1, 0};
    vpadStatus.dir.z = glm::fvec3{0, 0, 1};
    vpadStatus.accXY = {1.0f, 0.0f};

    // write the input back to VPADStatus
    writeMemory(vpadStatusOffset, &vpadStatus);

    // set r3 to 1 for hooked VPADRead function to return success
    hCPU->gpr[3] = 1;
}


// some ideas:
// - quickly pressing the grip button without a weapon while there's a nearby weapon and there's enough slots = pick up weapon
// - holding the grip button without a weapon while there's a nearby weapon = temporarily hold weapon
// - holding the grip button a weapon equipped = opens weapon dpad menu
// - quickly press the grip button while holding a weapon = drops current weapon

void CemuHooks::hook_CreateNewActor(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    // if (VRManager::instance().XR->GetRenderer() == nullptr || VRManager::instance().XR->GetRenderer()->m_layer3D.GetStatus() == RND_Renderer::Layer3D::Status3D::UNINITIALIZED) {
    //     hCPU->gpr[3] = 0;
    //     return;
    // }
    hCPU->gpr[3] = 0;

    // OpenXR::InputState inputs = VRManager::instance().XR->m_input.load();
    // if (!inputs.inGame.in_game) {
    //     hCPU->gpr[3] = 0;
    //     return;
    // }
    //
    // // test if controller is connected
    // if (inputs.inGame.grab[OpenXR::EyeSide::LEFT].currentState == XR_TRUE && inputs.inGame.grab[OpenXR::EyeSide::LEFT].changedSinceLastSync == XR_TRUE) {
    //     Log::print("Trying to spawn new thing!");
    //     hCPU->gpr[3] = 1;
    // }
    // else if (inputs.inGame.grab[OpenXR::EyeSide::RIGHT].currentState == XR_TRUE && inputs.inGame.grab[OpenXR::EyeSide::RIGHT].changedSinceLastSync == XR_TRUE) {
    //     Log::print("Trying to spawn new thing!");
    //     hCPU->gpr[3] = 1;
    // }
    // else {
    //     hCPU->gpr[3] = 0;
    // }
}