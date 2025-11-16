#include "instance.h"
#include "cemu_hooks.h"
#include "hands.h"


std::array<WeaponMotionAnalyser, 2> CemuHooks::m_motionAnalyzers = {};
std::array<uint32_t, 2> CemuHooks::m_heldWeapons = { 0, 0 };
std::array<uint32_t, 2> CemuHooks::m_heldWeaponsLastUpdate = { 0, 0 };

std::array s_cameraRotations = {
    glm::identity<glm::fquat>(),
    glm::identity<glm::fquat>()
};
std::array s_cameraPositions = {
    glm::fvec3(0.0f),
    glm::fvec3(0.0f)
};

static void ModifyWeaponMtxToVRPose(OpenXR::EyeSide side, BEMatrix34& toBeAdjustedMtx, glm::fquat cameraRotation, glm::fvec3 cameraPosition) {
    OpenXR::InputState inputs = VRManager::instance().XR->m_input.load();

    glm::fvec3 views = glm::fvec3(0, 0, 0);

    glm::fvec3 controllerPos = glm::fvec3(0.0f);
    glm::fquat controllerQuat = glm::identity<glm::fquat>();

    if (inputs.inGame.in_game && inputs.inGame.pose[side].isActive) {
        auto& handPose = inputs.inGame.poseLocation[side];

        if (handPose.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {
            controllerPos = ToGLM(handPose.pose.position);
        }
        if (handPose.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
            controllerQuat = ToGLM(handPose.pose.orientation);
        }
    }

    // Calculate the rotation
    glm::fquat rotatedControllerQuat = glm::normalize(cameraRotation * controllerQuat);

    glm::fvec3 v_controller = cameraRotation * controllerPos;
    glm::fvec3 v_cam = cameraRotation * views;
    glm::fvec3 v_controller_local = v_controller - v_cam;

    rotatedControllerQuat = glm::rotate(rotatedControllerQuat, glm::radians(180.0f), glm::fvec3(1.0f, 0.0f, 0.0f));
    rotatedControllerQuat = glm::rotate(rotatedControllerQuat, glm::radians(180.0f), glm::fvec3(0.0f, 0.0f, 1.0f));
    glm::fmat3 finalMtx = glm::mat3_cast(glm::inverse(rotatedControllerQuat) * glm::inverse(VRManager::instance().XR->m_inputCameraRotation.load()));

    toBeAdjustedMtx.x_x = finalMtx[0][0];
    toBeAdjustedMtx.y_x = finalMtx[0][1];
    toBeAdjustedMtx.z_x = finalMtx[0][2];

    toBeAdjustedMtx.x_y = finalMtx[1][0];
    toBeAdjustedMtx.y_y = finalMtx[1][1];
    toBeAdjustedMtx.z_y = finalMtx[1][2];

    toBeAdjustedMtx.x_z = finalMtx[2][0];
    toBeAdjustedMtx.y_z = finalMtx[2][1];
    toBeAdjustedMtx.z_z = finalMtx[2][2];

    glm::fvec3 rotatedControllerPos = v_controller_local * glm::inverse(VRManager::instance().XR->m_inputCameraRotation.load()) + v_cam;
    glm::fvec3 finalPos = cameraPosition + rotatedControllerPos;

    //Log::print("!! camera pos = {} rotation = {}", finalPos, views);
    //finalPos = v_cam + glm::fvec3(0, 1, 0);

    toBeAdjustedMtx.pos_x = finalPos.x;
    toBeAdjustedMtx.pos_y = finalPos.y;
    toBeAdjustedMtx.pos_z = finalPos.z;
}

void CemuHooks::hook_ChangeWeaponMtx(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    // r3 holds the source actor pointer
    // r4 holds the bone name
    // r5 holds the matrix that is to be set
    // r6 holds the extra matrix that is to be set
    // r7 holds the ModelBindInfo->mtx
    // r8 holds the target actor pointer
    // r9 returns 1 if the weapon is modified
    // r10 holds the camera pointer

    hCPU->gpr[9] = 0; // this is used to indicate whether the weapon was modified
    hCPU->gpr[11] = 0; // this is used to drop the weapon if the grip button is pressed

    if (GetSettings().IsThirdPersonMode()) {
        return;
    }

    uint32_t actorPtr = hCPU->gpr[3]; // holder of weapon
    uint32_t boneNamePtr = hCPU->gpr[4];
    uint32_t weaponMtxPtr = hCPU->gpr[5];
    uint32_t playerMtxPtr = hCPU->gpr[6];
    uint32_t modelBindInfoMtxPtr = hCPU->gpr[7];
    uint32_t targetActorPtr = hCPU->gpr[8]; // weapon that's being held
    uint32_t cameraPtr = hCPU->gpr[10];

    sead::FixedSafeString40 actorName = getMemory<sead::FixedSafeString40>(actorPtr + offsetof(ActorWiiU, name));

    // todo: remove this?
    BESeadLookAtCamera camera = {};
    readMemory(cameraPtr, &camera);
    glm::fvec3 cameraPos = camera.pos.getLE();
    glm::fvec3 cameraAt = camera.at.getLE();
    glm::fquat lookAtQuat = glm::quatLookAtRH(glm::normalize(cameraAt - cameraPos), { 0.0, 1.0, 0.0 });
    glm::fvec3 lookAtPos = cameraPos;
    lookAtPos.y += GetSettings().playerHeightSetting.getLE();

    // read bone name
    if (boneNamePtr == 0)
        return;
    char* boneName = (char*)s_memoryBaseAddress + boneNamePtr;

    // real logic
    bool isHeldByPlayer = actorName.getLE() == "GameROMPlayer";
    bool isLeftHandWeapon = strcmp(boneName, "Weapon_L") == 0;
    bool isRightHandWeapon = strcmp(boneName, "Weapon_R") == 0;
    if (!actorName.getLE().empty() && boneName[0] != '\0' && isHeldByPlayer && (isLeftHandWeapon || isRightHandWeapon)) {
        OpenXR::EyeSide side = isLeftHandWeapon ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;

        m_heldWeapons[side] = targetActorPtr;
        m_heldWeaponsLastUpdate[side] = 0;

        BEMatrix34 weaponMtx = {};
        readMemory(weaponMtxPtr, &weaponMtx);

        BEMatrix34 modelBindInfoMtx = {};
        readMemory(modelBindInfoMtxPtr, &modelBindInfoMtx);

        //ModifyWeaponMtxToVRPose(side, weaponMtx, lookAtQuat, lookAtPos);

        s_cameraPositions[side] = lookAtPos;
        s_cameraRotations[side] = lookAtQuat;


        //// prevent weapon transparency
        //BEType<float> modelOpacity = 1.0f;
        //BEType<float> negativeOpacity = 0.0f;
        //writeMemory(targetActorPtr + offsetof(ActorWiiU, modelOpacity), &modelOpacity);
        //writeMemory(targetActorPtr + offsetof(ActorWiiU, startModelOpacity), &modelOpacity);
        //writeMemory(targetActorPtr + offsetof(ActorWiiU, modelOpacityRelated), &negativeOpacity);
        //uint8_t opacityOrDoFlushOpacityToGPU = 1;
        //writeMemory(targetActorPtr + offsetof(ActorWiiU, opacityOrDoFlushOpacityToGPU), &opacityOrDoFlushOpacityToGPU);

        //writeMemory(weaponMtxPtr, &weaponMtx);
        //writeMemory(modelBindInfoMtxPtr, &modelBindInfoMtx);

        Weapon targetActor = {};
        readMemory(targetActorPtr, &targetActor);

        // check if weapon is held and if the grip button is held, drop it
        auto input = VRManager::instance().XR->m_input.load();
        auto& grabState = input.inGame.grabState[side];

        if (input.inGame.in_game && grabState.lastEvent == GrabButtonState::Event::DoublePress) {
            Log::print<CONTROLS>("Dropping weapon {} due to double press on grab button", targetActor.name.getLE().c_str());
            hCPU->gpr[11] = 1;
            hCPU->gpr[9] = 1;
            hCPU->gpr[13] = isLeftHandWeapon ? 1 : 0; // set the hand index to 0 for left hand, 1 for right hand
            return;
        }
        // Support for long press (placeholder)
        if (input.inGame.in_game && grabState.lastEvent == GrabButtonState::Event::LongPress) {
            Log::print<CONTROLS>("Long press detected for {} (side {})", targetActor.name.getLE().c_str(), (int)side);
            // TODO: Implement long press action (e.g., temporarily bind item)
            //grabState.longPress = false;
        }
        // Support for short press (placeholder)
        if (input.inGame.in_game && grabState.lastEvent == GrabButtonState::Event::ShortPress) {
            Log::print<CONTROLS>("Short press detected for {} (side {})", targetActor.name.getLE().c_str(), (int)side);
            // TODO: Implement short press action (e.g., cycle weapon)
        }

        hCPU->gpr[9] = 1;
    }
}

// todo: this only runs when it's shown for the first time!
void CemuHooks::hook_CreateNewScreen(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    const char* screenName = (const char*)(s_memoryBaseAddress + hCPU->gpr[7]);
    ScreenId screenId = (ScreenId)hCPU->gpr[5];
    Log::print<CONTROLS>("Switching to new screen \"{}\" with ID {:08X}...", screenName, std::to_underlying(screenId));

    // todo: When a pickup screen is shown, we should track if the user does a short grip press, and if it was the left and right hand.
    if (screenId == ScreenId::PickUp_00) {
        Log::print<CONTROLS>("PickUp screen detected, waiting for grip button press to bind item to hand...");
    }
}

// todo: this function does nothing, we use ChangeWeaponMtx instead
void CemuHooks::hook_DropEquipment(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    if (GetSettings().IsThirdPersonMode()) {
        return;
    }

    uint32_t weaponPtr = hCPU->gpr[3];
    uint32_t parentActorPtr = hCPU->gpr[4];
    uint32_t heldIndex = hCPU->gpr[5]; // this is either 0 or 1 depending on which hand the weapon is in
    bool isHeldByPlayer = hCPU->gpr[6] == 0;

    Weapon weapon = {};
    readMemory(weaponPtr, &weapon);

    //// check if weapon is held and if the grip button is held, drop it
    //auto input = VRManager::instance().XR->m_input.load();
    //if (input.inGame.in_game && isHeldByPlayer && input.inGame.grab[heldIndex].currentState) {
    //    // if the weapon is held by the player and the grip button is pressed, drop it
    //    //Log::print("!! Dropping weapon {} because grip button is pressed", weapon.name.getLE());
    //    hCPU->gpr[7] = 1;
    //    return;
    //}
}

uint32_t frameIndex = 0;

void CemuHooks::hook_EnableWeaponAttackSensor(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    if (GetSettings().IsThirdPersonMode()) {
        return;
    }

    frameIndex++;
    if (frameIndex % 2 == 0) {
        return;
    }

    uint32_t weaponPtr = hCPU->gpr[3];
    uint32_t parentActorPtr = hCPU->gpr[4];
    uint32_t heldIndex = hCPU->gpr[5]; // this is either 0 or 1 depending on which hand the weapon is in
    bool isHeldByPlayer = hCPU->gpr[6] == 0;

    Weapon weapon = {};
    readMemory(weaponPtr, &weapon);

    WeaponType weaponType = weapon.type.getLE();
    if (weaponType == WeaponType::Bow || weaponType == WeaponType::Shield) {
        //Log::print("!! Skipping motion analysis for Bow/Shield (type: {})", (int)weaponType);
        return;
    }

    if (heldIndex >= m_motionAnalyzers.size()) {
        Log::print<CONTROLS>("Invalid heldIndex: {}. Skipping motion analysis.", heldIndex);
        return;
    }

    heldIndex = heldIndex == 0 ? 1 : 0;

    
    //Log::print("!! Running weapon analysis for {}", heldIndex);

    auto state = VRManager::instance().XR->m_input.load();
    auto headset = VRManager::instance().XR->GetRenderer()->GetMiddlePose();
    if (!headset.has_value()) {
        return;
    }

    m_motionAnalyzers[heldIndex].ResetIfWeaponTypeChanged(weaponType);
    m_motionAnalyzers[heldIndex].Update(state.inGame.poseLocation[heldIndex], state.inGame.poseVelocity[heldIndex], headset.value(), state.inGame.inputTime);

    // Use the analysed motion to determine whether the weapon is swinging or stabbing, and whether the attackSensor should be active this frame
    bool CHEAT_alwaysEnableWeaponCollision = true;
    if (isHeldByPlayer && (m_motionAnalyzers[heldIndex].IsAttacking() || CHEAT_alwaysEnableWeaponCollision)) {
        m_motionAnalyzers[heldIndex].SetHitboxEnabled(true);
        //Log::print("!! Activate sensor for {}: isHeldByPlayer={}, weaponType={}", heldIndex, isHeldByPlayer, (int)weaponType);
        weapon.setupAttackSensor.resetAttack = 1;
        weapon.setupAttackSensor.mode = 2;
        weapon.setupAttackSensor.isContactLayerInitialized = 0;
        // weapon.setupAttackSensor.overrideImpact = 1;
        // weapon.setupAttackSensor.multiplier = analyzer->GetDamage();
        // weapon.setupAttackSensor.impact = analyzer->GetImpulse();
        writeMemory(weaponPtr, &weapon);
    }
    else if (m_motionAnalyzers[heldIndex].IsHitboxEnabled()) {
        m_motionAnalyzers[heldIndex].SetHitboxEnabled(false);
        //Log::print("!! Deactivate sensor for {}: isHeldByPlayer={}, weaponType={}", heldIndex, isHeldByPlayer, (int)weaponType);

        weapon.setupAttackSensor.resetAttack = 1;
        weapon.setupAttackSensor.mode = 1; // deactivate attack sensor
        weapon.setupAttackSensor.isContactLayerInitialized = 0;
        writeMemory(weaponPtr, &weapon);
    }
}

void CemuHooks::hook_SetPlayerWeaponScale(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    if (GetSettings().IsThirdPersonMode()) {
        return;
    }

    //uint32_t weaponPtr = hCPU->gpr[31];
    //Weapon weapon = {};
    //readMemory(weaponPtr, &weapon);
    //WeaponType weaponType = weapon.type.getLE();

    ////Log::print<INFO>("Setting weapon scale to {} for weapon {} of type {}", weapon.originalScale.getLE(), weapon.name.getLE().c_str(), std::to_underlying(weaponType));
    //
    ////weapon.originalScale = glm::fvec3(0.9f, 0.9f, 0.9f);

    //writeMemory(weaponPtr, &weapon);
}

void CemuHooks::hook_EquipWeapon(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    auto input = VRManager::instance().XR->m_input.load();
    // Check both hands for a short press to pick up weapon
    for (int side = 0; side < 2; ++side) {
        auto& grabState = input.inGame.grabState[side];
        // todo: Make sword smaller while its equipped. I think this might be a member value, but otherwise we can just scale the weapon matrix.

        //if (input.inGame.in_game && grabState.shortPress) {
        //    // Set the slot to equip based on which hand was pressed
        //    hCPU->gpr[25] = side; // 0 = LEFT, 1 = RIGHT
        //    Log::print("!! Short grip press detected on side {}: equipping weapon", side);
        //    grabState.shortPress = false; // Reset after use
        //    return;
        //}
    }
    // Default behavior if no short press
    // (leave as is, or add fallback logic if needed)
}


void CemuHooks::hook_DropWeaponLogging(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    uint32_t actorPtr = hCPU->gpr[3];

    PlayerOrEnemy player;
    readMemory(actorPtr, &player);

    uint32_t actorLinkPtr = actorPtr + offsetof(ActorWiiU, name) + offsetof(sead::FixedSafeString40, c_str);
    uint32_t actorNamePtr = 0;
    readMemoryBE(actorLinkPtr, &actorNamePtr);
    if (actorNamePtr == 0)
        return;
    char* actorName = (char*)s_memoryBaseAddress + actorNamePtr;

    uint32_t weaponIdx = hCPU->gpr[4];
    BEVec3 position;
    readMemory(hCPU->gpr[5], &position);
    uint32_t a4 = hCPU->gpr[6];
    uint32_t a5 = hCPU->gpr[7];
    uint32_t a6 = hCPU->gpr[8];
    uint32_t a7 = hCPU->gpr[9];

    Log::print<CONTROLS>("{} ({:08X}) is dropping weapon with idx={}, position={}, a4={}, a5={}, a6={}, a7={}", actorName, actorPtr, weaponIdx, position, a4, a5, a6, a7);
}

void CemuHooks::hook_ModifyHandModelAccessSearch(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    if (hCPU->gpr[3] == 0) {
        return;
    }
#ifdef _DEBUG
    // r3 holds the address of the string to search for
    const char* actorName = (const char*)(s_memoryBaseAddress + hCPU->gpr[3]);

    if (actorName != nullptr) {
        // Weapon_R is presumably his right hand bone name
        Log::print<CONTROLS>("Searching for model handle using {}", actorName);
    }
#endif
}

void CemuHooks::DrawDebugOverlays() {
    if (ImGui::Begin("Weapon Motion Debugger")) {
        for (auto it = m_motionAnalyzers.rbegin(); it != m_motionAnalyzers.rend(); ++it) {
            ImGui::PushID(&(*it));
            ImGui::BeginGroup();
            it->DrawDebugOverlay();
            ImGui::EndGroup();
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void CemuHooks::hook_ModifyBoneMatrix(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    uint32_t gsysModelPtr = hCPU->gpr[3];
    uint32_t matrixPtr = hCPU->gpr[4];
    uint32_t scalePtr = hCPU->gpr[5];
    uint32_t boneNamePtr = hCPU->gpr[6];
    uint32_t boneIdx = hCPU->gpr[27];

    sead::FixedSafeString100 modelName = getMemory<sead::FixedSafeString100>(gsysModelPtr + 0x128);

    BEMatrix34 matrix = {};
    readMemory(matrixPtr, &matrix);
    BEVec3 scale = {};
    readMemory(scalePtr, &scale);

    if (gsysModelPtr == 0 || matrixPtr == 0 || scalePtr == 0) {
        //Log::print("!! Bone name couldn't be found for bone #{} for model {}", boneIdx, modelName.getLE());
        return;
    }

    if (boneNamePtr == 0) {
        // #53 is neck
        // #90 is
        // #93 is leg
        // #106 is left leg
        // #107 is left ankle
        // #108 is also left leg
        // #110 is left foot
        // #111 is left toes
        // #112 is right leg
        // #113 is right ankle
        // #114 is leg, ankle downwards
        // #115
        //Log::print("!! Bone name couldn't be found for bone #{} for model {}", boneIdx, modelName.getLE());
        return;
    }
    else {
        char* boneNamePtrChar = (char*)(s_memoryBaseAddress + boneNamePtr);
        std::string_view boneName(boneNamePtrChar);
        //Log::print("!! Bone index #{} ({}) for model {}", boneIdx, boneName, modelName.getLE());
    }

    // todo: check if Link's armor is filtered out due to this check
    if (modelName.getLE() != "GameROMPlayer") {
        return;
    }


    char* boneNamePtrChar = (char*)(s_memoryBaseAddress + boneNamePtr);
    std::string_view boneName(boneNamePtrChar);
    //Log::print("!! Scale = {} for {}", scale.getLE(), boneName);

    bool leftOrRightSide = boneName.ends_with("_L");

    OpenXR::EyeSide side = leftOrRightSide ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;
    OpenXR::InputState inputs = VRManager::instance().XR->m_input.load();

    glm::fvec3 controllerPos = glm::fvec3(0.0f);
    glm::fquat controllerQuat = glm::identity<glm::fquat>();
    glm::fvec3 relativeControllerPos = glm::fvec3(0.0f);
    glm::fquat relativeControllerQuat = glm::identity<glm::fquat>();
    if (inputs.inGame.in_game && inputs.inGame.pose[side].isActive) {
        auto& handPose = inputs.inGame.poseLocation[side];

        if (handPose.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {
            controllerPos = ToGLM(handPose.pose.position);
        }
        if (handPose.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
            controllerQuat = ToGLM(handPose.pose.orientation);
        }

        auto& relativeHandPose = inputs.inGame.hmdRelativePoseLocation[side];

        if (handPose.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {
            relativeControllerPos = ToGLM(relativeHandPose.pose.position);
        }
        if (handPose.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
            relativeControllerQuat = ToGLM(relativeHandPose.pose.orientation);
        }
    }

#ifdef _DEBUG
    static glm::fquat debug_rotatingAngles = glm::identity<glm::fquat>();
    debug_rotatingAngles = debug_rotatingAngles * glm::angleAxis(glm::radians(0.1f), glm::fvec3(0, 0, 1));

    static glm::fvec3 debug_movingPositions = glm::fvec3();
    static bool isMovingUp = true;
    glm::fvec3 moveAxis = glm::fvec3(0.5f, 0.0f, 0.0f);
    float speedOverSecond = 1.0f / 240.0f;

    glm::fvec3 dir = glm::normalize(moveAxis);
    float step = speedOverSecond * glm::length(moveAxis);
    if (isMovingUp) {
        debug_movingPositions += dir * step;
        float t = glm::dot(debug_movingPositions, dir);
        if (t >= 0.5f) {
            debug_movingPositions = dir * 0.5f;
            isMovingUp = false;
        }
    }
    else {
        debug_movingPositions -= dir * step;
        float t = glm::dot(debug_movingPositions, dir);
        if (t <= -0.5f) {
            debug_movingPositions = dir * -0.5f;
            isMovingUp = true;
        }
    }
    //debug_movingPositions = glm::fvec3(0, 1, 0);
#endif

    if (boneName == "Root") {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());
    }
    else if (boneName == "Skl_Root") {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());

        //matrix.setPos(glm::fvec3(0.0f, 0.99426f, 0.0f));
    }
    else if (boneName.starts_with("Spine_1")) {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());
        //matrix.setRotLE(matrix.getRotLE() * glm::angleAxis(glm::radians(-90.0f), glm::fvec3(0, 0, 1)));
        //matrix.setRotLE(matrix.getRotLE() * glm::angleAxis(glm::radians(-90.0f), glm::fvec3(1, 0, 0)));
        //matrix.setRotLE(matrix.getRotLE() * glm::angleAxis(glm::radians(-90.0f), glm::fvec3(0, 0, 1)));
    }
    else if (boneName.starts_with("Spine_2")) {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());

        //matrix.setPos(glm::fvec3(0.13600f, 0.0f, 0.0f));
    }
    else if (boneName == "Neck") {
        //matrix.setPos(glm::fvec3(0.26326f, 0.0f, 0.0f));

        // fixme: properly hide his head instead of just putting it way below.
        //matrix.setPos(glm::fvec3(-0.46326f, 0.0f, 0.0f));
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());
    }
    else if (boneName == "Head") {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());

        //matrix.setPos(glm::fvec3(0.12447f, 0.0f, 0.0f));
    }
    else if (boneName.starts_with("Clavicle_Assist")) {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());
    }
    else if (boneName.starts_with("Clavicle")) {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());
    }
    else if (boneName.starts_with("Arm_1_Assist")) {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());
    }
    else if (boneName.starts_with("Arm_1")) {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());
    }
    else if (boneName.starts_with("Elbow")) {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());
    }
    else if (boneName.starts_with("Wrist_Assist")) {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());
    }
    else if (boneName.starts_with("Arm_2")) {
        matrix.setPos(glm::fvec3());
        matrix.setRotLE(glm::identity<glm::fquat>());

        // add player height that we've removed above
        //matrix.setPos(matrix.getPos().getLE() - glm::fvec3(0.0f, 0.99426f + 0.13600f, 0.0f));
    }
    else if (boneName.starts_with("Wrist")) {
        matrix.setRotLE(glm::identity<glm::fquat>());
        matrix.setPos(glm::fvec3());

        // player model orientation in world space
        BEMatrix34 bePlayerMtx = getMemory<BEMatrix34>(s_playerMtxAddress);
        glm::mat4x3 playerMtx = bePlayerMtx.getLEMatrix();
        glm::mat4 playerMtx4 = glm::mat4(playerMtx);
        glm::mat4 playerPos = glm::translate(glm::identity<glm::fmat4>(), glm::fvec3(playerMtx[3]));
        glm::mat4 playerRot = glm::mat4_cast(glm::quat_cast(playerMtx4));

        BEMatrix34 mtx = {};
        readMemory(s_playerMtxAddress, &mtx);

        glm::fquat handCorrectionQuat = glm::identity<glm::fquat>();
        // todo: what's the correct hand holding offset here? angle between grip and pointing pose?
        if (leftOrRightSide) {
            handCorrectionQuat *= glm::angleAxis(glm::radians(90.0f), glm::fvec3(0, 1, 0));
            handCorrectionQuat *= glm::angleAxis(glm::radians(-90.0f), glm::fvec3(0, 0, 1));
            handCorrectionQuat *= glm::angleAxis(glm::radians(-45.0f), glm::fvec3(1, 0, 0));
            handCorrectionQuat *= glm::angleAxis(glm::radians(45.0f), glm::fvec3(1, 0, 0));
        }
        else {
            handCorrectionQuat *= glm::angleAxis(glm::radians(-90.0f), glm::fvec3(0, 0, 1));
            handCorrectionQuat *= glm::angleAxis(glm::radians(-180.0f), glm::fvec3(0, 1, 0));
            handCorrectionQuat *= glm::angleAxis(glm::radians(270.0f), glm::fvec3(1, 0, 0));
        }
        // slightly tweak it to align better to VR pose
        handCorrectionQuat *= glm::angleAxis(glm::radians(20.0f), glm::fvec3(0, 0, 1));

        glm::fmat4 handCorrections = glm::mat4_cast(handCorrectionQuat);


        // calculate the inverse offset from the Weapon_L/Weapon_R bones, since the XR pose is meant to be at that position, and this function only is able to access the parent wrist bone
        // Weapon_L: 0.10690f 0.00002f 0.02769f, rotation = euler(1.57080f, 0.0f, 3.14159f)
        // Weapon_R: -0,10690 -0,00002 -0,02769, rotation = euler(1.57080f, 0.0f, 0.0f)
        // todo: double-check rotation of VR controller in hands before release; quick check
        glm::fvec3 weaponPositionOffset = leftOrRightSide ? glm::fvec3(0.10690f, 0.00002f, 0.02769f) : glm::fvec3(-0.10690, -0.00002, -0.02769f);
        glm::fvec3 weaponRotationOffset = leftOrRightSide ? glm::fvec3(1.57080f, 0.0f, 3.14159f) : glm::fvec3(1.57080f, 0.0f, 0.0f);
        weaponPositionOffset *= -1.0f;
        glm::fmat4 weaponOffsetMtx = glm::translate(glm::identity<glm::fmat4>(), weaponPositionOffset); // * glm::toMat4(glm::quat(weaponRotationOffset));

        // turn the controller pose into a matrix
        glm::fmat4 vrTransformationMatrix = glm::translate(glm::identity<glm::fmat4>(), controllerPos);
        glm::fmat4 vrRotationMatrix = glm::mat4_cast(controllerQuat);
        glm::fmat4 controllerInLocalSpace = vrTransformationMatrix * vrRotationMatrix * handCorrections;

        // camera matrix from the game
        // todo: this might cause the camera to be behind a frame, so find a better way to get the camera matrix
        //BEMatrix34 beCameraMtx = {};
        //readMemory(s_cameraMtxAddress, &mtx);
        glm::mat4 cameraMtx = s_lastCameraMtx;
        glm::mat4 cameraPositionOnlyMtx = glm::translate(glm::identity<glm::fmat4>(), glm::fvec3(cameraMtx[3]));
        glm::fquat cameraQuat = glm::quat_cast(cameraMtx);
        glm::mat4 cameraRotationOnlyMtx = glm::mat4_cast(cameraQuat);

        // calculate the weapon matrix
        glm::mat4 controllerInWorld = cameraRotationOnlyMtx * controllerInLocalSpace * weaponOffsetMtx;

        glm::fmat4 controllerRelativeToPlayerModel = playerPos * controllerInWorld;

        glm::fmat4x3 truncatedFinalMtx = glm::mat4x3(glm::inverse(playerMtx4) * controllerRelativeToPlayerModel);

        matrix.setLEMatrix(truncatedFinalMtx);
    }

    writeMemory(matrixPtr, &matrix);
}
