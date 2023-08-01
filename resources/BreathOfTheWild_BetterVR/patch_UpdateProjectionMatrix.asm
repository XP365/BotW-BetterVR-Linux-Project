[BetterVR_UpdateProjectionMatrix_V208]
moduleMatches = 0x6267BFD0

.origin = codecave


ASPECT_STACK_OFFSET = 0x04
FOVY_STACK_OFFSET = 0x08
OFFSET_X_STACK_OFFSET = 0x0C
OFFSET_Y_STACK_OFFSET = 0x10


updateCameraOffset:
mflr r0
stwu r1, -0x18(r1)
stw r0, 0x1C(r1)

addi r11, r1, 0x04
bl import.coreinit.hook_UpdateCameraOffset


lwz r11, 0xCD4(r29)
lfs f10, FOVY_STACK_OFFSET(r1)
stfs f10, 0x68(r11)
lfs f9, OFFSET_X_STACK_OFFSET(r1)
stfs f9, 0x6C(r11)
lfs f10, OFFSET_Y_STACK_OFFSET(r1)
stfs f10, 0x70(r11)

lwz r0, 0x1C(r1)
mtlr r0
addi r1, r1, 0x18
blr

0x02C07708 = bla updateCameraOffset


updateCameraAspectRatio:
mflr r0
stwu r1, -0x10(r1)
stw r0, 0x14(r1)

addi r28, r1, 0x04
bl import.coreinit.hook_CalculateCameraAspectRatio

lfs f12, ASPECT_STACK_OFFSET(r1)
stfs f12, 0xB4(r30)

lwz r0, 0x14(r1)
mtlr r0
addi r1, r1, 0x10
blr

0x0386D024 = bla updateCameraAspectRatio



; -----------------------------------------------------------

loadLineCharacter:
.int 10
.align 4
; r0 should be modifiable
; r3 = format string
; r4 = int arg1
; r5 = int arg2
; f1 = float arg1
; f2 = float arg2
printToLog:
mflr r0
stwu r1, -0x10(r1)
stw r0, 0x14(r1)
stw r5, 0x8(r1)
stw r6, 0xC(r1)

lis r6, loadLineCharacter@ha
lwz r6, loadLineCharacter@l(r6)
crxor 4*cr1+eq, 4*cr1+eq, 4*cr1+eq
bl import.coreinit.OSReport

lwz r6, 0xC(r1)
lwz r5, 0x8(r1)
lwz r0, 0x14(r1)
mtlr r0
addi r1, r1, 0x10
blr

loadLineFormat:
.string "sead::PerspectiveProjection::setFovy_(this = 0x%08x fovRadians = %f offsetX = %f) LR = 0x%08x %c"
printSetFovY:
mflr r0
stwu r1, -0x10(r1)
stw r0, 0x14(r1)

stw r3, 0x8(r1)
stw r4, 0xC(r1)
stw r5, 0x10(r1)

; load offset X from this sead::PerspectiveProjection instance
fmr f31, f2
lfs f2, 0xB0(r3)

mr r4, r3
lwz r5, 0x10+0x24(r1) ; load LR from parent stack
lis r3, loadLineFormat@ha
addi r3, r3, loadLineFormat@l
bl printToLog

fmr f2, f31
fmuls f31, f1, f0

lwz r3, 0x8(r1)
lwz r4, 0xC(r1)
lwz r5, 0x10(r1)

lwz r0, 0x14(r1)
mtlr r0
addi r1, r1, 0x10
blr

0x030C16F8 = bla printSetFovY
