/*
 * elf_symbol.h
 *
 * Copyright (c) 1991-2020, Linus Torvalds and Linux Kernel project contributors.
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nxdumptool is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __ELF_SYMBOL_H__
#define __ELF_SYMBOL_H__

/* Symbol types. */
#define STT_NOTYPE              0           /* The symbol type not defined. */
#define STT_OBJECT              1           /* The symbol is associated with a data object. */
#define STT_FUNC                2           /* The symbol is associated with a function or other executable code. */
#define STT_SECTION             3           /* The symbol is associated with a section. */
#define STT_FILE                4           /* The symbol's name gives the name of the source file associated with the object file. */
#define STT_COMMON              5           /* The symbol labels an uninitialized common block. */
#define STT_TLS                 6           /* The symbol specifies a Thread-Local Storage entity. */
#define STT_LOOS                10          /* Reserved for operating system-specific semantics. */
#define STT_HIOS                12          /* Reserved for operating system-specific semantics. */
#define STT_LOPROC              13          /* Reserved for processor-specific semantics. */
#define STT_HIPROC              15          /* Reserved for processor-specific semantics. */

/* Symbol binding attributes. */
#define STB_LOCAL               0           /* Local symbols are not visible outside the object file containing their definition. */
#define STB_GLOBAL              1           /* Global symbols are visible to all object files being combined. */
#define STB_WEAK                2           /* Weak symbols resemble global symbols, but their definitions have lower precedence. */
#define STB_LOOS                10          /* Reserved for operating system-specific semantics. */
#define STB_HIOS                12          /* Reserved for operating system-specific semantics. */
#define STB_LOPROC              13          /* Reserved for processor-specific semantics. */
#define STB_HIPROC              15          /* Reserved for processor-specific semantics. */

/* Symbol visibility. */
#define STV_DEFAULT             0           /* The symbol visibility is specified by the its binding attribute. */
#define STV_INTERNAL            1           /* The meaning of this visibility attribute may be defined by processor supplements to further constrain hidden symbols. */
#define STV_HIDDEN              2           /* The symbol name is not visible to other components. */
#define STV_PROTECTED           3           /* The symbol is visible in other components but not preemptable. */

/* Section header index. */
#define SHN_UNDEF               0           /* The symbol is undefined. */
#define SHN_LORESERVE           0xFF00
#define SHN_LOPROC              0xFF00
#define SHN_HIPROC              0xFF1F
#define SHN_LOOS                0xFF20
#define SHN_LIVEPATCH           0xFF20
#define SHN_HIOS                0xFF3F
#define SHN_ABS                 0xFFF1      /* The symbol has an absolute value that will not change because of relocation. */
#define SHN_COMMON              0xFFF2      /* Labels a common block that has not yet been allocated. */
#define SHN_XINDEX              0xFFFF      /* The symbol refers to a specific location within a section, but the section header index is too large to be represented directly. */
#define SHN_HIRESERVE           0xFFFF

/* Macros. */
#define ELF_ST_BIND(x)          ((x) >> 4)  /* Extracts binding value from a st_info field. */
#define ELF_ST_TYPE(x)          ((x) & 0xF) /* Extracts type value from a st_info field. */
#define ELF_ST_VISIBILITY(x)    ((x) & 0x3) /* Extracts visibility value from a st_other field. */

typedef struct {
    u32 st_name;    ///< Symbol name offset within dynamic string table.
    u32 st_value;   ///< Symbol value.
    u32 st_size;    ///< Symbol size.
    u8 st_info;     ///< Symbol type (lower nibble) and binding attributes (upper nibble).
    u8 st_other;    ///< Currently specifies a symbol's visibility  (lower 3 bits).
    u16 st_shndx;   ///< Holds the relevant section header table index.
} Elf32Symbol;

NXDT_ASSERT(Elf32Symbol, 0x10);

typedef struct {
    u32 st_name;    ///< Symbol name offset within dynamic string table.
    u8 st_info;     ///< Symbol type (lower nibble) and binding attributes (upper nibble).
    u8 st_other;    ///< Currently specifies a symbol's visibility (lower 3 bits).
    u16 st_shndx;   ///< Holds the relevant section header table index.
    u64 st_value;   ///< Symbol value.
    u64 st_size;    ///< Symbol size.
} Elf64Symbol;

NXDT_ASSERT(Elf64Symbol, 0x18);

#endif /* __ELF_SYMBOL_H__ */
