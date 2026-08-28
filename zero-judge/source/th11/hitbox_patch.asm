.386
.model flat
option casemap:none

; Source for the injected x86 payload. The PowerShell patch embeds the
; assembled bytes, so end users do not need MASM.

PLAYER_PTR      equ 004A8EB4h
PROBE_FLAG      equ 004CAE40h
PLAYER_HALF_X   equ 08E4h
PLAYER_HALF_Y   equ 08E8h
PLAYER_HIT      equ 00432A90h

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

; Keep the original result unless it is a hit. Hits are recomputed with
; zero player extents; rejected hits become graze results.
probe_wrapper:
    push ebp
    mov ebp, esp
    push esi
    sub esp, 10h

    mov dword ptr [ebp-8], eax
    mov ecx, dword ptr ds:[PLAYER_PTR]
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
    mov eax, dword ptr [ebp-0Ch]
    mov edx, PLAYER_HIT
    call edx
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

; Execute the six overwritten entry bytes and continue in the originals.
trampoline_rotated_a:
    push ebp
    mov ebp, esp
    and esp, 0FFFFFFF8h
    push 00432076h
    ret

trampoline_rotated_b:
    push ebp
    mov ebp, esp
    and esp, 0FFFFFFF8h
    push 004322F6h
    ret

trampoline_rotated_c:
    push ebp
    mov ebp, esp
    and esp, 0FFFFFFF8h
    push 00432526h
    ret

; Probe calls suppress PlayerHit. Normal calls tail-jump to the original.
player_hit_gate:
    cmp byte ptr ds:[PROBE_FLAG], 0
    jne gate_return
    push PLAYER_HIT
    ret
gate_return:
    ret

payload_end label byte

end
