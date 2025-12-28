#pragma once
#include "entity_debugger.h"


class CemuHooks {
public:
    CemuHooks() {
        m_cemuHandle = GetModuleHandleA(NULL);
        checkAssert(m_cemuHandle != NULL, "Failed to get handle of Cemu process which is required for interfacing with Cemu!");

        gameMeta_getTitleId = (gameMeta_getTitleIdPtr_t)GetProcAddress(m_cemuHandle, "gameMeta_getTitleId");
        memory_getBase = (memory_getBasePtr_t)GetProcAddress(m_cemuHandle, "memory_getBase");
        osLib_registerHLEFunction = (osLib_registerHLEFunctionPtr_t)GetProcAddress(m_cemuHandle, "osLib_registerHLEFunction");
        checkAssert(gameMeta_getTitleId != nullptr && memory_getBase != nullptr && osLib_registerHLEFunction != nullptr, "Failed to get function pointers of Cemu functions! Is this hook being used on Cemu?");

        bool isSupportedTitleId = gameMeta_getTitleId() == 0x00050000101C9300 || gameMeta_getTitleId() == 0x00050000101C9400 || gameMeta_getTitleId() == 0x00050000101C9500;
        checkAssert(isSupportedTitleId, std::format("Expected title IDs for Breath of the Wild (00050000-101C9300, 00050000-101C9400 or 00050000-101C9500) but received {:16x}!", gameMeta_getTitleId()).c_str());

        s_memoryBaseAddress = (uint64_t)memory_getBase();
        checkAssert(s_memoryBaseAddress != 0, "Failed to get memory base address of Cemu process!");


        osLib_registerHLEFunction("coreinit", "hook_UpdateSettings", &hook_UpdateSettings);

        // Actor Hooks
        osLib_registerHLEFunction("coreinit", "hook_UpdateActorList", &hook_UpdateActorList);
        osLib_registerHLEFunction("coreinit", "hook_CreateNewActor", &hook_CreateNewActor);

        // Camera Hooks
        osLib_registerHLEFunction("coreinit", "hook_BeginCameraSide", &hook_BeginCameraSide);
        osLib_registerHLEFunction("coreinit", "hook_ModifyLightPrePassProjectionMatrix", &hook_ModifyLightPrePassProjectionMatrix);
        osLib_registerHLEFunction("coreinit", "hook_UpdateCameraForGameplay", &hook_UpdateCameraForGameplay);
        osLib_registerHLEFunction("coreinit", "hook_GetRenderCamera", &hook_GetRenderCamera);
        osLib_registerHLEFunction("coreinit", "hook_GetRenderProjection", &hook_GetRenderProjection);
        osLib_registerHLEFunction("coreinit", "hook_EndCameraSide", &hook_EndCameraSide);

        osLib_registerHLEFunction("coreinit", "hook_UseCameraDistance", &hook_UseCameraDistance);
        osLib_registerHLEFunction("coreinit", "hook_ReplaceCameraMode", &hook_ReplaceCameraMode);
        osLib_registerHLEFunction("coreinit", "hook_GetEventName", &hook_GetEventName);
        osLib_registerHLEFunction("coreinit", "hook_OverwriteCameraParam", &hook_OverwriteCameraParam);
        osLib_registerHLEFunction("coreinit", "hook_PlayerLadderFix", &hook_PlayerLadderFix);

        // First-Person Model Hooks
        osLib_registerHLEFunction("coreinit", "hook_SetActorOpacity", &hook_SetActorOpacity);
        osLib_registerHLEFunction("coreinit", "hook_CalculateModelOpacity", &hook_CalculateModelOpacity);
        osLib_registerHLEFunction("coreinit", "hook_ModifyBoneMatrix", &hook_ModifyBoneMatrix);
        osLib_registerHLEFunction("coreinit", "hook_ChangeWeaponMtx", &hook_ChangeWeaponMtx);

        // First-Person Weapon Hooks
        osLib_registerHLEFunction("coreinit", "hook_EquipWeapon", &hook_EquipWeapon);
        osLib_registerHLEFunction("coreinit", "hook_DropEquipment", &hook_DropEquipment);
        osLib_registerHLEFunction("coreinit", "hook_EnableWeaponAttackSensor", &hook_EnableWeaponAttackSensor);
        osLib_registerHLEFunction("coreinit", "hook_SetPlayerWeaponScale", &hook_SetPlayerWeaponScale);
        osLib_registerHLEFunction("coreinit", "hook_GetContactLayerOfAttack", &hook_GetContactLayerOfAttack);

        // Input Hooks
        osLib_registerHLEFunction("coreinit", "hook_InjectXRInput", &hook_InjectXRInput);
        osLib_registerHLEFunction("coreinit", "hook_XRRumble_VPADControlMotor", &hook_XRRumble_VPADControlMotor);
        osLib_registerHLEFunction("coreinit", "hook_XRRumble_VPADStopMotor", &hook_XRRumble_VPADStopMotor);

        // Logging/Debugging Hooks
        osLib_registerHLEFunction("coreinit", "hook_OSReportToConsole", &hook_OSReportToConsole);
        osLib_registerHLEFunction("coreinit", "hook_DropWeaponLogging", &hook_DropWeaponLogging);
        osLib_registerHLEFunction("coreinit", "hook_ModifyHandModelAccessSearch", &hook_ModifyHandModelAccessSearch);
        osLib_registerHLEFunction("coreinit", "hook_CreateNewScreen", &hook_CreateNewScreen);
        osLib_registerHLEFunction("coreinit", "hook_RouteActorJob", &hook_RouteActorJob);
    };
    ~CemuHooks() {
        FreeLibrary(m_cemuHandle);
    };

    static data_VRSettingsIn GetSettings();
    static uint64_t GetMemoryBaseAddress() { return s_memoryBaseAddress; }    

    std::unique_ptr<class EntityDebugger> m_entityDebugger;
    static std::array<class WeaponMotionAnalyser, 2> m_motionAnalyzers;
    static std::array<uint32_t, 2> m_heldWeapons;
    static std::array<uint32_t, 2> m_heldWeaponsLastUpdate;
    static uint32_t s_playerAddress;
    static uint32_t s_playerMtxAddress;
    static uint32_t s_cameraMtxAddress;
    static glm::fvec3 s_playerPos;
    static glm::mat4 s_lastCameraMtx;

    // If the user is unable to control the camera, we can guess that they're in a cutscene
    struct HybridEventSettings {
        bool firstPerson;                  // use Link's perspective, ignore the animated event camera
        bool disablePlayerDrivenLinkHands; // let event control the hands instead of the VR controllers
        bool ignoreCameraRotation;         // some events will pan the camera, but in first-person it should usually be ignored to avoid nausea. Doors opening is okay, but panning down to a chest is not.
        bool demoEnableCameraInput;        // there's already events that allow user camera control. This isn't used or overwritten atm.
    };

    static uint32_t GetFramesSinceLastCameraUpdate() { return s_framesSinceLastCameraUpdate.load(); }
    static bool IsInGame() {
        // todo: check if 3 frames is the right threshold
        return GetFramesSinceLastCameraUpdate() <= 4;
    }

    static std::string s_currentEvent;
    static HybridEventSettings s_currentEventSettings;
    static std::unordered_map<std::string, HybridEventSettings> s_eventSettings;
    static void initCutsceneDefaultSettings(uint32_t ppc_TableOfCutsceneEventsSettingsOffset);

    static bool HasActiveCutscene() {
        return !s_currentEvent.empty();
    }

    static EventMode GetEventModeWithOverride() {
        if (!HasActiveCutscene()) {
            return EventMode::NO_EVENT;
        }

        EventMode mode = GetSettings().GetCutsceneCameraMode();
        // todo: check if user has overriden the cutscene mode during active cutscenes

        // if the camera is controllable, treat it as no event
        // todo: Apparently this is a bad way to check it.
        if (IsInGame()) {
            //Log::print<VERBOSE>("Camera is controllable during cutscene '{}' due to frames since last camera update being {}. Treating as no event.", s_currentEvent, GetFramesSinceLastCameraUpdate());
            //return EventMode::NO_EVENT;
        }
        return mode;
    }

    static std::optional<HybridEventSettings> GetFirstPersonSettingsForActiveEvent() {
        auto mode = GetEventModeWithOverride();

        if (mode == EventMode::NO_EVENT) {
            return std::nullopt;
        }
        if (mode == EventMode::ALWAYS_THIRD_PERSON) {
            return std::nullopt;
        }
        // resolve settings if in hybrid mode
        if (mode == EventMode::FOLLOW_DEFAULT_EVENT_SETTINGS) {
            //if (!s_currentEventSettings.firstPerson) {
            //    return std::nullopt;
            //}
        }

        return s_currentEventSettings;
    }

    static bool IsFirstPerson() {
        if (auto settings = GetFirstPersonSettingsForActiveEvent(); settings.has_value()) {
            // there's an active event
            auto mode = GetEventModeWithOverride();
            if (mode == EventMode::FOLLOW_DEFAULT_EVENT_SETTINGS && !settings->firstPerson) {
                return false;
            }
            // cutscene with first-person settings
            return true;
        }
        else {
            // no event. Check if gameplay is in first-person mode
            if (GetSettings().IsFirstPersonMode()) {
                return true;
            }
            return false;
        }
    }

    static bool IsThirdPerson() {
        return !IsFirstPerson();
    }

    static bool UseBlackBarsDuringEvents() {
        if (!HasActiveCutscene() || IsFirstPerson()) {
            return false;
        }

        return GetSettings().UseBlackBarsForCutscenes();
    }

    static void DrawDebugOverlays();

private:
    HMODULE m_cemuHandle;

    osLib_registerHLEFunctionPtr_t osLib_registerHLEFunction;
    memory_getBasePtr_t memory_getBase;
    gameMeta_getTitleIdPtr_t gameMeta_getTitleId;

    static uint64_t s_memoryBaseAddress;
    static std::atomic_uint32_t s_framesSinceLastCameraUpdate;

    static void hook_UpdateSettings(PPCInterpreter_t* hCPU);

    // Actor Hooks
    static void hook_UpdateActorList(PPCInterpreter_t* hCPU);
    static void hook_CreateNewActor(PPCInterpreter_t* hCPU);

    // Camera Hooks
    static void hook_BeginCameraSide(PPCInterpreter_t* hCPU);
    static void hook_ModifyLightPrePassProjectionMatrix(PPCInterpreter_t* hCPU);
    static void hook_UpdateCameraForGameplay(PPCInterpreter_t* hCPU);
    static void hook_GetRenderCamera(PPCInterpreter_t* hCPU);
    static void hook_GetRenderProjection(PPCInterpreter_t* hCPU);
    static void hook_EndCameraSide(PPCInterpreter_t* hCPU);

    static void hook_UseCameraDistance(PPCInterpreter_t* hCPU);
    static void hook_ReplaceCameraMode(PPCInterpreter_t* hCPU);
    static void hook_GetEventName(PPCInterpreter_t* hCPU);
    static void hook_OverwriteCameraParam(PPCInterpreter_t* hCPU);
    static void hook_PlayerLadderFix(PPCInterpreter_t* hCPU);

    // First-Person Model Hooks
    static void hook_SetActorOpacity(PPCInterpreter_t* hCPU);
    static void hook_CalculateModelOpacity(PPCInterpreter_t* hCPU);
    static void hook_ModifyBoneMatrix(PPCInterpreter_t* hCPU);
    static void hook_ChangeWeaponMtx(PPCInterpreter_t* hCPU);

    // First-Person Weapon Hooks
    static void hook_EquipWeapon(PPCInterpreter_t* hCPU);
    static void hook_DropEquipment(PPCInterpreter_t* hCPU);
    static void hook_EnableWeaponAttackSensor(PPCInterpreter_t* hCPU);
    static void hook_SetPlayerWeaponScale(PPCInterpreter_t* hCPU);
    static void hook_GetContactLayerOfAttack(PPCInterpreter_t* hCPU);

    // Input Hooks
    static void hook_InjectXRInput(PPCInterpreter_t* hCPU);
    static void hook_XRRumble_VPADControlMotor(PPCInterpreter_t* hCPU);
    static void hook_XRRumble_VPADStopMotor(PPCInterpreter_t* hCPU);

    // Logging/Debugging Hooks
    static void hook_OSReportToConsole(PPCInterpreter_t* hCPU);
    static void hook_DropWeaponLogging(PPCInterpreter_t* hCPU);
    static void hook_ModifyHandModelAccessSearch(PPCInterpreter_t* hCPU);
    static void hook_CreateNewScreen(PPCInterpreter_t* hCPU);
    static void hook_RouteActorJob(PPCInterpreter_t* hCPU);

public:
    template <typename T>
    static void writeMemoryBE(uint64_t offset, T* valuePtr) {
        *valuePtr = swapEndianness(*valuePtr);
        memcpy((void*)(s_memoryBaseAddress + offset), (void*)valuePtr, sizeof(T));
    }

    template <typename T>
    static void writeMemory(uint64_t offset, T* valuePtr) {
        memcpy((void*)(s_memoryBaseAddress + offset), (void*)valuePtr, sizeof(T));
    }

    template <typename T>
    static void readMemoryBE(uint64_t offset, T* resultPtr) {
        uint64_t memoryAddress = s_memoryBaseAddress + offset;
        memcpy(resultPtr, (void*)memoryAddress, sizeof(T));
        *resultPtr = swapEndianness(*resultPtr);
    }

    template <typename T>
    static void readMemory(uint64_t offset, T* resultPtr) {
        uint64_t memoryAddress = s_memoryBaseAddress + offset;
        memcpy(resultPtr, (void*)memoryAddress, sizeof(T));
    }

    template <typename T>
    static auto getMemory(uint64_t offset) {
        if constexpr (is_BEType_v<T>) {
            T result;
            readMemory(offset, &result);
            return result;
        }
        else {
            BEType<T> result;
            readMemory(offset, &result);
            return result;
        }
    }
};
