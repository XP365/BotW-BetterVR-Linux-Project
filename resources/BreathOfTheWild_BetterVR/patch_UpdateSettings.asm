[BetterVR_UpdateSettings_V208]
moduleMatches = 0x6267BFD0

.origin = codecave


data_settingsOffset:
CameraModeSetting:
.int $cameraMode

GUIFollowModeSetting:
.int $guiFollowMode

AlternatingEyeRenderingSetting:
.int $AER


vr_updateSettings:
addi r1, r1, -0x08
stw r3, 0x04(r1)
stw r5, 0x08(r1)
mflr r5
stw r5, 0x0C(r1)

lis r5, data_settingsOffset@ha
addi r5, r5, data_settingsOffset@l
bl import.coreinit.hook_UpdateSettings

; spawn check
li r3, 0
bl import.coreinit.hook_CreateNewActor
cmpwi r3, 1
bne notSpawnActor
bl vr_spawnEquipment
notSpawnActor:

li r4, -1 ; Execute the instruction that got replaced

lwz r5, 0x0C(r1)
mtlr r5
lwz r5, 0x08(r1)
lwz r3, 0x04(r1)
addi r1, r1, 0x08
b 0x031faaf4

0x031FAAF0 = ba vr_updateSettings