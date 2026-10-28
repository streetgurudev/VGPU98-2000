; A PE test DLL for vglink: per-process sections, export names as written
; (from a list file) and a version resource, as a Glide DLL has them
; (tools/tests/CMakeLists.txt, pe.lnk). Checked by drvlint and by
; vglink-pe.cmake.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

section .text code

global _DllMain@12
global _TestOne@4
global __TestTwo@8

_DllMain@12:
    mov eax, 1
    ret 12

_TestOne@4:
    mov eax, [value]                    ; an absolute address: a base relocation
    ret 4

; A stdcall function whose C name starts with an underscore (as Glide's
; _grGetSipValues): its symbol has two.
__TestTwo@8:
    xor eax, eax
    ret 8

section .data data

value   dd 1234
pointer dd _TestOne@4
