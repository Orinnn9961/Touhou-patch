.386
.model flat
option casemap:none

; Source for the injected x86 payload. The PowerShell patch embeds the
; assembled bytes, so end users do not need MASM.

PLAYER_PTR      equ 004B4514h
PROBE_FLAG      equ 004D6460h
PLAYER_HALF_X   equ 09E4h
PLAYER_HALF_Y   equ 09E8h
PLAYER_HIT      equ 00438370h

.code

payload_start label byte

stub_rotated_a:
    push 0
    jmp short probe_wrapper

stub_rotated_b:
    push 1
    jmp short probe_wrapper

stub_rotated_c:
    push 2

; The original collision result is retained unless it is a hit. A hit is
; recomputed with zero player extents; a rejected hit becomes graze (2),
; preserving the result that the original radius would have produced.
probe_wrapper:
    push ebp
    mov ebp, esp
    push esi
    sub esp, 10h

    mov dword ptr [ebp-8], eax
    mov eax, dword ptr [ebp+4]
    cmp eax, 2
    je use_member_player
    mov ecx, dword ptr ds:[PLAYER_PTR]
use_member_player:
    mov dword ptr [ebp-0Ch], ecx

    call run_probe
    cmp eax, 1
    jne wrapper_done

    mov ecx, dword ptr [ebp-0Ch]
    mov edx, dword ptr [ecx+PLAYER_HALF_X]
    mov dword ptr [ebp-10h], edx
    mov edx, dword ptr [ecx+PLAYER_HALF_Y]
    mov dword ptr [ebp-14h], edx
    mov dword ptr [ecx+PLAYER_HALF_X], 0
    mov dword ptr [ecx+PLAYER_HALF_Y], 0

    call run_probe

    mov ecx, dword ptr [ebp-0Ch]
    mov edx, dword ptr [ebp-10h]
    mov dword ptr [ecx+PLAYER_HALF_X], edx
    mov edx, dword ptr [ebp-14h]
    mov dword ptr [ecx+PLAYER_HALF_Y], edx

    cmp eax, 1
    je confirmed_hit
    mov eax, 2
    jmp short wrapper_done

confirmed_hit:
    cmp dword ptr [ebp+4], 1
    je confirmed_result
    mov esi, dword ptr [ebp-0Ch]
    mov eax, PLAYER_HIT
    call eax
confirmed_result:
    mov eax, 1

wrapper_done:
    lea esp, [ebp-4]
    pop esi
    pop ebp
    pop ecx
    ret 0Ch

run_probe:
    push dword ptr [ebp+14h]
    push dword ptr [ebp+10h]
    push dword ptr [ebp+0Ch]
    mov byte ptr ds:[PROBE_FLAG], 1
    mov eax, dword ptr [ebp-8]
    mov ecx, dword ptr [ebp-0Ch]
    mov edx, dword ptr [ebp+4]
    test edx, edx
    je call_rotated_a
    cmp edx, 1
    je call_rotated_b
    call trampoline_rotated_c
    jmp short probe_return
call_rotated_a:
    call trampoline_rotated_a
    jmp short probe_return
call_rotated_b:
    call trampoline_rotated_b
probe_return:
    mov byte ptr ds:[PROBE_FLAG], 0
    ret

; These execute the six bytes replaced by each entry jump, then continue in
; the original function. push/ret preserves the collision input in EAX.
trampoline_rotated_a:
    push ebp
    mov ebp, esp
    and esp, 0FFFFFFF8h
    push 00437A86h
    ret

trampoline_rotated_b:
    push ebp
    mov ebp, esp
    and esp, 0FFFFFFF8h
    push 00437D06h
    ret

trampoline_rotated_c:
    push ebp
    mov ebp, esp
    and esp, 0FFFFFFF8h
    push 00437F36h
    ret

; The rotated collision functions call this instead of killing the player.
; Probe runs suppress the side effect; a normal call tail-jumps to PlayerHit.
player_hit_gate:
    cmp byte ptr ds:[PROBE_FLAG], 0
    jne gate_return
    push PLAYER_HIT
    ret
gate_return:
    ret

payload_end label byte

end
