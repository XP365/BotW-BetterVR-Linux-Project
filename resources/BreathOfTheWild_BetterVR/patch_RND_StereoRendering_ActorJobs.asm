[BetterVR_StereoRendering_ActorJobs_V208]
moduleMatches = 0x6267BFD0

.origin = codecave

; timeout for skipping job pushes
; normally the game skips job pushes by setting this counter to 2
; however, in stereo rendering this code is executed twice per frame (once per eye)
; so this timeout is now doubled to 4 to account for both eyes
; this fixes LinkTag and EventTag actors not updating properly in stereo rendering
; which breaks dungeon doors and many other things
0x037A1B3C = li r12, 4

; EventTag::shouldSkipJobPush
;0x0310C670 = lis r3, currentEyeSide@ha
;0x0310C674 = li r3, 1 ; lwz r3, currentEyeSide@l(r3)
;0x0310C678 = blr

; LinkTag::shouldSkipJobPush
;0x031216BC = lis r3, currentEyeSide@ha
;0x031216C0 = li r3, 1 ; lwz r3, currentEyeSide@l(r3)
;0x031216C4 = blr

; should skip job push for baseproc
;0x0378BD2C = li r3, 0


custom_Actor_decrementSkipJobPushTimer:
lis r12, currentEyeSide@ha
lwz r12, currentEyeSide@l(r12)
cmpwi r12, 1
;beqlr

lbz r12, 0x42C(r3)
cmpwi r12, 0
beqlr
addi r0, r12, -1
stb r0, 0x42C(r3)
blr

;0x0379E270 = ba custom_Actor_decrementSkipJobPushTimer

; =====================================================

hook_sub_37B524C:
lis r12, currentEyeSide@ha
lwz r12, currentEyeSide@l(r12)
cmpwi r12, 1
beqlr

mr r12, r3
lwz r11, 0x1C(r12)
cmpwi r11, 0
beqlr
lha r0, 0x22(r12)
cmpwi r0, 0
beqlr
lha r10, 0x20(r12)
cmpwi r0, 0
add r3, r11, r10
bge loc_37B5284
lwz r11, 0x24(r12)
mtctr r11
bctr

loc_37B5284:
lha r12, 0x26(r12)
lwzx r10, r3, r12
slwi r0, r0, 3
add r9, r10, r0
lwz r11, 4(r9)
mtctr r11
bctr
blr

;0x037B524C = ba hook_sub_37B524C

; ======================================================

0x0310C82C = Actor__deleteIfPlacementStuff:

custom_EventTag__calcAi:
mflr r0
stwu r1, -0x10(r1)
stw r31, 0x0C(r1)
stw r0, 0x14(r1)

lis r31, currentEyeSide@ha
lwz r31, currentEyeSide@l(r31)
cmpwi r31, 1
beq loc_310C85C ; skip if right eye

lis r31, Actor__deleteIfPlacementStuff@ha
addi r31, r31, Actor__deleteIfPlacementStuff@l
mtctr r31
mr r31, r3
bctrl ; bl Actor__deleteIfPlacementStuff
cmpwi r3, 0
bne loc_310C85C
lwz r3, 0x390(r31)
cmpwi r3, 0
beq loc_310C854
lwz r12, 0xC(r3)
lwz r0, 0xA4(r12)
mtctr r0
bctrl

loc_310C854:
li r0, 0
stb r0, 0x58C(r31)

loc_310C85C:
lwz r0, 0x14(r1)
lwz r31, 0x0C(r1)
mtlr r0
addi r1, r1, 0x10
blr

;0x0310C818 = ba custom_EventTag__calcAi

; ======================================================


0x031222F8 = LinkTag_job_run:

custom_LinkTag_job_run:
mflr r0
stwu r1, -0x0C(r1)
stw r0, 0x10(r1)
stw r31, 0x08(r1)
stw r30, 0x04(r1)

; skip if right eye
lis r31, currentEyeSide@ha
lwz r31, currentEyeSide@l(r31)
cmpwi r31, 1
beq exit_LinkTag_job_run

; call original function
lis r30, LinkTag_job_run@ha
addi r30, r30, LinkTag_job_run@l
mtctr r30
bctrl ; bl LinkTag_job_run

exit_LinkTag_job_run:
lwz r0, 0x10(r1)
mtlr r0
lwz r31, 0x08(r1)
lwz r30, 0x04(r1)
addi r1, r1, 0x0C
blr

;0x0311F404 = lis r26, custom_LinkTag_job_run@ha
;0x0311F420 = addi r26, r26, custom_LinkTag_job_run@l


; ======================================================
; ======================================================

0x03415C6C = ksys__CalcAttentionAndVibration:
0x03414FE8 = ksys__CalcPlayReportAndStatsMgr:
0x1046CA64 = ActorDebug__sInstance:
0x03414BF8 = ksys__CalcQuestAndEventAndActorDeleteOrPreload:

ksys__CalcFrameJob1__run:
mflr r0
stw r0, 0x04(r1)
stwu r1, -0x08(r1)

lis r12, currentEyeSide@ha
lwz r12, currentEyeSide@l(r12)
cmpwi r12, 1
beq exit_ksys__CalcFrameJob1__run ; skip if right eye

lis r12, ksys__CalcAttentionAndVibration@ha
addi r12, r12, ksys__CalcAttentionAndVibration@l
mtctr r12
bctrl ; bl ksys__CalcAttentionAndVibration
lis r12, ksys__CalcPlayReportAndStatsMgr@ha
addi r12, r12, ksys__CalcPlayReportAndStatsMgr@l
mtctr r12
bctrl ; bl ksys__CalcPlayReportAndStatsMgr
lis r12, ActorDebug__sInstance@ha
lwz r12, ActorDebug__sInstance@l(r12)
cmpwi r12, 0
beq loc_31FDC38
lwz r0, 0x18(r12)

;rotlwi r0, r0, 2
;li r12, 1
;and r0, r0, r12
;cmpwi cr0, r0, 0

.long 0x540017FF
beq exit_ksys__CalcFrameJob1__run

loc_31FDC38:
lis r12, ksys__CalcQuestAndEventAndActorDeleteOrPreload@ha
addi r12, r12, ksys__CalcQuestAndEventAndActorDeleteOrPreload@l
mtctr r12
bctrl ; bl ksys__CalcQuestAndEventAndActorDeleteOrPreload

exit_ksys__CalcFrameJob1__run:
lwz r0, 0x0C(r1)
mtlr r0
addi r1, r1, 8
blr


;0x031FDC08 = ba ksys__CalcFrameJob1__run


; ======================================================

0x0313EC98 = ksys__map__ObjectLinkArray__checkLink:

custom_actor_checkSignal:
;bla import.coreinit.log_actorCheckSignal
lwz r12, 0x4FC(r3)
cmpwi r12, 0
beq loc_379F1F4
lwz r0, 0x28(r12)
cmpwi r0, 0
bne loc_379F1FC

loc_379F1F4:
li r3, 0
blr

loc_379F1FC:
lwz r12, 0x28(r12)
li r5, 0
lis r3, ksys__map__ObjectLinkArray__checkLink@ha
addi r3, r3, ksys__map__ObjectLinkArray__checkLink@l
mtctr r3
addi r3, r12, 0x20
bctr ; b ksys__map__ObjectLinkArray__checkLink
blr

; ======================================================
; ======================================================
; ======================================================

0x037A5DB0 = real_actor_job0_1:

hook_actor_job0_1:
mr r0, r3

lis r3, currentEyeSide@ha
lwz r3, currentEyeSide@l(r3)
cmpwi r3, 1
bne .+0x0C ; don't early return if left eye
mr r3, r0
blr

lis r3, real_actor_job0_1@ha
addi r3, r3, real_actor_job0_1@l
mtctr r3
mr r3, r0
bctr ; ba real_actor_job0_1
blr

0x03795750 = lis r27, hook_actor_job0_1@ha
0x03795760 = addi r27, r27, hook_actor_job0_1@l

; ======================================================

0x037A7D3C = real_actor_job0_2:

hook_actor_job0_2:
mr r0, r3

lis r3, currentEyeSide@ha
lwz r3, currentEyeSide@l(r3)
cmpwi r3, 1
bne .+0x0C ; don't early return if left eye
mr r3, r0
blr

lis r3, real_actor_job0_2@ha
addi r3, r3, real_actor_job0_2@l
mtctr r3
mr r3, r0
bctr ; ba real_actor_job0_2
blr

0x0379575C = lis r25, hook_actor_job0_2@ha
0x03795768 = addi r25, r25, hook_actor_job0_2@l

; ======================================================

0x037A6EC4 = real_actor_job1_1:

hook_actor_job1_1:
mr r0, r3

lis r3, currentEyeSide@ha
lwz r3, currentEyeSide@l(r3)
cmpwi r3, 1
bne .+0x0C ; don't early return if left eye
mr r3, r0
blr

lis r3, real_actor_job1_1@ha
addi r3, r3, real_actor_job1_1@l
mtctr r3
mr r3, r0
bctr ; ba real_actor_job1_1
blr

0x03795834 = lis r25, hook_actor_job1_1@ha
0x03795840 = addi r25, r25, hook_actor_job1_1@l

; ======================================================

0x037A7CB8 = real_actor_job1_2:

hook_actor_job1_2:
mr r0, r3

lis r3, currentEyeSide@ha
lwz r3, currentEyeSide@l(r3)
cmpwi r3, 1
bne .+0x0C ; don't early return if left eye
mr r3, r0
blr

lis r3, real_actor_job1_2@ha
addi r3, r3, real_actor_job1_2@l
mtctr r3
mr r3, r0
bctr ; ba real_actor_job1_2
blr

0x03795844 = lis r20, hook_actor_job1_2@ha
0x03795850 = addi r20, r20, hook_actor_job1_2@l

; ======================================================

0x037A7438 = real_actor_job2_1_ragdoll_related:

hook_actor_job2_1_ragdoll_related:
mr r0, r3

lis r3, currentEyeSide@ha
lwz r3, currentEyeSide@l(r3)
cmpwi r3, 1
bne .+0x0C ; don't early return if left eye
mr r3, r0
blr

lis r3, real_actor_job2_1_ragdoll_related@ha
addi r3, r3, real_actor_job2_1_ragdoll_related@l
mtctr r3
mr r3, r0
bctr ; ba real_actor_job2_1_ragdoll_related
blr

0x037958F8 = lis r20, hook_actor_job2_1_ragdoll_related@ha
0x03795904 = addi r20, r20, hook_actor_job2_1_ragdoll_related@l

; ======================================================

0x037A7E30 = real_actor_job2_2:

hook_actor_job2_2:
lis r12, currentEyeSide@ha
lwz r12, currentEyeSide@l(r12)
cmpwi r12, 1
beqlr

lis r12, real_actor_job2_2@ha
addi r12, r12, real_actor_job2_2@l
mtctr r12
bctr ; ba real_actor_job2_2
blr

0x03795908 = lis r19, hook_actor_job2_2@ha
0x03795914 = addi r19, r19, hook_actor_job2_2@l

; ======================================================

0x037A7C00 = real_actor_job4:

hook_actor_job4:
mr r0, r3

lis r3, currentEyeSide@ha
lwz r3, currentEyeSide@l(r3)
cmpwi r3, 1
bne .+0x0C ; don't early return if left eye
mr r3, r0
blr

lis r3, real_actor_job4@ha
addi r3, r3, real_actor_job4@l
mtctr r3
mr r3, r0
bctr ; ba real_actor_job4
blr

0x037959C0 = lis r21, hook_actor_job4@ha
0x037959D0 = addi r21, r21, hook_actor_job4@l