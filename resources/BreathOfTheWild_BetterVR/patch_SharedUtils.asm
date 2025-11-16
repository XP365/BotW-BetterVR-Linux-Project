[BetterVR_Utils_V208]
moduleMatches = 0x6267BFD0

.origin = codecave

; String Comparison Function (r3 = string1, r4 = string2, sets comparison register so use beq for true, bne for false)
_compareString:
stwu r1, -0x20(r1)
mflr r0
stw r0, 0x04(r1)
stw r3, 0x0C(r1)
stw r4, 0x10(r1)
stw r5, 0x14(r1)
stw r6, 0x18(r1)


startLoop:
lbz r5, 0(r3)
lbz r6, 0(r4)

cmpwi r5, 0
bne checkForMatch
cmpwi r6, 0
bne checkForMatch
b foundMatch

checkForMatch:
cmpw r5, r6
bne noMatch
addi r3, r3, 1
addi r4, r4, 1
b startLoop

noMatch:
; this sets the comparison register to 0 aka false
li r5, 0
cmpwi r5, 1337
b end

foundMatch:
li r5, 1337
cmpwi r5, 1337
b end

end:
lwz r3, 0x0C(r1)
lwz r4, 0x10(r1)
lwz r5, 0x14(r1)
lwz r6, 0x18(r1)
lwz r0, 0x04(r1)
mtlr r0
addi r1, r1, 0x20
blr


; Log to OSReport using format string
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
stwu r1, -0x40(r1)
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
addi r1, r1, 0x40 ; this was set to 0x10 before, but that makes no sense?
blr

; =======================================================================================================================

0x03A14E1C = vsnprintfToSafeString:

; r3 = reserved
; r4 = reserved
; r5 = format string passed by callee
; r6 = first integer arg
; f1 = first float arg
; r7 = second integer arg
; f2 = second float arg

printToCemuConsoleWithFormat:
; outer stack frame
mflr r0
stwu r1, -0x1040(r1)
stw r0, 0x24(r1)
stw r3, 0x1C(r1)
stw r4, 0x18(r1)
stw r5, 0x14(r1)
stw r6, 0x10(r1)

; temp string buffer
addi r3, r1, 0x400
li r4, 0x200
; r5 = format string
; r6 = args
.int 0x4CC63182 ; crclr 4*cr1+eq
bla import.coreinit.__os_snprintf
; returns r3 = length of new string
; string is in r1+0x1020

addi r3, r1, 0x400
bla import.coreinit.hook_OSReportToConsole

; restore outer stack frame
lwz r6, 0x10(r1)
lwz r5, 0x14(r1)
lwz r4, 0x18(r1)
lwz r3, 0x1C(r1)
lwz r0, 0x24(r1)
addi r1, r1, 0x1040
mtlr r0
blr
