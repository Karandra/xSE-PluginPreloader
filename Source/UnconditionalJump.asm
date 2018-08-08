.data
extern UnconditionalJumpAddress: qword

.code

UnconditionalJump proc
jmp qword ptr [UnconditionalJumpAddress]
UnconditionalJump endp

end
