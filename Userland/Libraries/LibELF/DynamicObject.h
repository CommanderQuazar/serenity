/*
 * Copyright (c) 2019-2020, Andrew Kaster <andrewdkaster@gmail.com>
 * Copyright (c) 2020, Itamar S. <itamar8910@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <AK/Assertions.h>
#include <AK/RefCounted.h>
#include <Kernel/VirtualAddress.h>
#include <LibELF/exec_elf.h>

namespace ELF {

class DynamicObject : public RefCounted<DynamicObject> {
public:
    static NonnullRefPtr<DynamicObject> construct(VirtualAddress base_address, VirtualAddress dynamic_section_address);

    ~DynamicObject();
    void dump() const;

    class DynamicEntry;
    class Section;
    class RelocationSection;
    class Symbol;
    class Relocation;
    class HashSection;

    class DynamicEntry {
    public:
        DynamicEntry(const Elf32_Dyn& dyn)
            : m_dyn(dyn)
        {
        }

        ~DynamicEntry() { }

        Elf32_Sword tag() const { return m_dyn.d_tag; }
        Elf32_Addr ptr() const { return m_dyn.d_un.d_ptr; }
        Elf32_Word val() const { return m_dyn.d_un.d_val; }

    private:
        const Elf32_Dyn& m_dyn;
    };

    class Symbol {
    public:
        Symbol(const DynamicObject& dynamic, unsigned index, const Elf32_Sym& sym)
            : m_dynamic(dynamic)
            , m_sym(sym)
            , m_index(index)
        {
            if (section_index() == 0)
                m_is_undefined = true;
        }

        Symbol(const Symbol& other)
            : m_dynamic(other.m_dynamic)
            , m_sym(other.m_sym)
            , m_index(other.m_index)
            , m_is_undefined(other.m_is_undefined)
        {
        }

        static Symbol create_undefined(const DynamicObject& dynamic)
        {
            auto s = Symbol(dynamic, 0, {});
            s.m_is_undefined = true;
            return s;
        }

        ~Symbol() { }

        const char* name() const { return m_dynamic.symbol_string_table_string(m_sym.st_name); }
        unsigned section_index() const { return m_sym.st_shndx; }
        unsigned value() const { return m_sym.st_value; }
        unsigned size() const { return m_sym.st_size; }
        unsigned index() const { return m_index; }
        unsigned type() const { return ELF32_ST_TYPE(m_sym.st_info); }
        unsigned bind() const { return ELF32_ST_BIND(m_sym.st_info); }

        bool is_undefined() const
        {
            return m_is_undefined;
        }
        VirtualAddress address() const
        {
            if (m_dynamic.elf_is_dynamic())
                return m_dynamic.base_address().offset(value());
            return VirtualAddress { value() };
        }
        const DynamicObject& object() const { return m_dynamic; }

    private:
        const DynamicObject& m_dynamic;
        const Elf32_Sym& m_sym;
        const unsigned m_index;
        bool m_is_undefined { false };
    };

    class Section {
    public:
        Section(const DynamicObject& dynamic, unsigned section_offset, unsigned section_size_bytes, unsigned entry_size, const char* name)
            : m_dynamic(dynamic)
            , m_section_offset(section_offset)
            , m_section_size_bytes(section_size_bytes)
            , m_entry_size(entry_size)
            , m_name(name)
        {
        }
        ~Section() { }

        const char* name() const { return m_name; }
        unsigned offset() const { return m_section_offset; }
        unsigned size() const { return m_section_size_bytes; }
        unsigned entry_size() const { return m_entry_size; }
        unsigned entry_count() const
        {
            return !entry_size() ? 0 : size() / entry_size();
        }
        VirtualAddress address() const
        {
            return m_dynamic.base_address().offset(m_section_offset);
        }

    protected:
        friend class RelocationSection;
        friend class HashSection;
        const DynamicObject& m_dynamic;
        unsigned m_section_offset;
        unsigned m_section_size_bytes;
        unsigned m_entry_size;
        const char* m_name { nullptr };
    };

    class RelocationSection : public Section {
    public:
        RelocationSection(const Section& section)
            : Section(section.m_dynamic, section.m_section_offset, section.m_section_size_bytes, section.m_entry_size, section.m_name)
        {
        }
        unsigned relocation_count() const { return entry_count(); }
        const Relocation relocation(unsigned index) const;
        const Relocation relocation_at_offset(unsigned offset) const;
        template<typename F>
        void for_each_relocation(F) const;
    };

    class Relocation {
    public:
        Relocation(const DynamicObject& dynamic, const Elf32_Rel& rel, unsigned offset_in_section)
            : m_dynamic(dynamic)
            , m_rel(rel)
            , m_offset_in_section(offset_in_section)
        {
        }

        ~Relocation() { }

        unsigned offset_in_section() const { return m_offset_in_section; }
        unsigned offset() const { return m_rel.r_offset; }
        unsigned type() const { return ELF32_R_TYPE(m_rel.r_info); }
        unsigned symbol_index() const { return ELF32_R_SYM(m_rel.r_info); }
        const Symbol symbol() const { return m_dynamic.symbol(symbol_index()); }
        VirtualAddress address() const
        {
            if (m_dynamic.elf_is_dynamic())
                return m_dynamic.base_address().offset(offset());
            return VirtualAddress { offset() };
        }

    private:
        const DynamicObject& m_dynamic;
        const Elf32_Rel& m_rel;
        const unsigned m_offset_in_section;
    };

    enum class HashType {
        SYSV,
        GNU
    };

    class HashSection : public Section {
    public:
        HashSection(const Section& section, HashType hash_type)
            : Section(section.m_dynamic, section.m_section_offset, section.m_section_size_bytes, section.m_entry_size, section.m_name)
        {
            switch (hash_type) {
            case HashType::SYSV:
                m_lookup_function = &HashSection::lookup_elf_symbol;
                break;
            case HashType::GNU:
                m_lookup_function = &HashSection::lookup_gnu_symbol;
                break;
            default:
                ASSERT_NOT_REACHED();
                break;
            }
        }

        const Symbol lookup_symbol(const char*) const;

    private:
        u32 calculate_elf_hash(const char* name) const;
        u32 calculate_gnu_hash(const char* name) const;

        const DynamicObject::Symbol lookup_elf_symbol(const char* name) const;
        const DynamicObject::Symbol lookup_gnu_symbol(const char* name) const;

        typedef const DynamicObject::Symbol (HashSection::*LookupFunction)(const char*) const;
        LookupFunction m_lookup_function;
    };

    unsigned symbol_count() const { return m_symbol_count; }

    const Symbol symbol(unsigned) const;

    typedef void (*InitializationFunction)();

    bool has_init_section() const { return m_init_offset != 0; }
    bool has_init_array_section() const { return m_init_array_offset != 0; }
    const Section init_section() const;
    InitializationFunction init_section_function() const;
    const Section fini_section() const;
    const Section init_array_section() const;
    const Section fini_array_section() const;

    const HashSection hash_section() const;

    const RelocationSection relocation_section() const;
    const RelocationSection plt_relocation_section() const;

    bool should_process_origin() const { return m_dt_flags & DF_ORIGIN; }
    bool requires_symbolic_symbol_resolution() const { return m_dt_flags & DF_SYMBOLIC; }
    // Text relocations meaning: we need to edit the .text section which is normally mapped PROT_READ
    bool has_text_relocations() const { return m_dt_flags & DF_TEXTREL; }
    bool must_bind_now() const { return m_dt_flags & DF_BIND_NOW; }
    bool has_static_thread_local_storage() const { return m_dt_flags & DF_STATIC_TLS; }

    VirtualAddress plt_got_base_address() const { return m_base_address.offset(m_procedure_linkage_table_offset); }
    VirtualAddress base_address() const { return m_base_address; }

    const char* soname() const { return m_has_soname ? symbol_string_table_string(m_soname_index) : nullptr; }

    Optional<FlatPtr> tls_offset() const { return m_tls_offset; }
    Optional<FlatPtr> tls_size() const { return m_tls_size; }
    void set_tls_offset(FlatPtr offset) { m_tls_offset = offset; }
    void set_tls_size(FlatPtr size) { m_tls_size = size; }

    template<typename F>
    void for_each_needed_library(F) const;

    template<typename F>
    void for_each_initialization_array_function(F f) const;

    struct SymbolLookupResult {
        bool found { false };
        FlatPtr value { 0 };
        FlatPtr address { 0 };
        unsigned bind { STB_LOCAL };
        const ELF::DynamicObject* dynamic_object { nullptr }; // The object in which the symbol is defined
    };
    SymbolLookupResult lookup_symbol(const char* name) const;

    // Will be called from _fixup_plt_entry, as part of the PLT trampoline
    Elf32_Addr patch_plt_entry(u32 relocation_offset);

    SymbolLookupResult lookup_symbol(const ELF::DynamicObject::Symbol& symbol) const;

    bool elf_is_dynamic() const { return m_is_elf_dynamic; }

private:
    explicit DynamicObject(VirtualAddress base_address, VirtualAddress dynamic_section_address);

    const char* symbol_string_table_string(Elf32_Word) const;
    void parse();

    template<typename F>
    void for_each_symbol(F) const;

    template<typename F>
    void for_each_dynamic_entry(F) const;

    VirtualAddress m_base_address;
    VirtualAddress m_dynamic_address;
    VirtualAddress m_elf_base_address;

    unsigned m_symbol_count { 0 };

    // Begin Section information collected from DT_* entries
    FlatPtr m_init_offset { 0 };
    FlatPtr m_fini_offset { 0 };

    FlatPtr m_init_array_offset { 0 };
    size_t m_init_array_size { 0 };
    FlatPtr m_fini_array_offset { 0 };
    size_t m_fini_array_size { 0 };

    FlatPtr m_hash_table_offset { 0 };
    HashType m_hash_type { HashType::SYSV };

    FlatPtr m_string_table_offset { 0 };
    size_t m_size_of_string_table { 0 };
    FlatPtr m_symbol_table_offset { 0 };
    size_t m_size_of_symbol_table_entry { 0 };

    Elf32_Sword m_procedure_linkage_table_relocation_type { -1 };
    FlatPtr m_plt_relocation_offset_location { 0 }; // offset of PLT relocations, at end of relocations
    size_t m_size_of_plt_relocation_entry_list { 0 };
    FlatPtr m_procedure_linkage_table_offset { 0 };

    // NOTE: We'll only ever either RELA or REL entries, not both (thank god)
    // NOTE: The x86 ABI will only ever genrerate REL entries.
    size_t m_number_of_relocations { 0 };
    size_t m_size_of_relocation_entry { 0 };
    size_t m_size_of_relocation_table { 0 };
    FlatPtr m_relocation_table_offset { 0 };
    bool m_is_elf_dynamic { false };

    // DT_FLAGS
    Elf32_Word m_dt_flags { 0 };

    bool m_has_soname { false };
    Elf32_Word m_soname_index { 0 }; // Index into dynstr table for SONAME

    Optional<FlatPtr> m_tls_offset;
    Optional<FlatPtr> m_tls_size;
    // End Section information from DT_* entries
};

template<typename F>
inline void DynamicObject::RelocationSection::for_each_relocation(F func) const
{
    for (unsigned i = 0; i < relocation_count(); ++i) {
        const auto reloc = relocation(i);
        if (reloc.type() == 0)
            continue;
        if (func(reloc) == IterationDecision::Break)
            break;
    }
}

template<typename F>
inline void DynamicObject::for_each_symbol(F func) const
{
    for (unsigned i = 0; i < symbol_count(); ++i) {
        if (func(symbol(i)) == IterationDecision::Break)
            break;
    }
}

template<typename F>
inline void DynamicObject::for_each_dynamic_entry(F func) const
{
    auto* dyns = reinterpret_cast<const Elf32_Dyn*>(m_dynamic_address.as_ptr());
    for (unsigned i = 0;; ++i) {
        auto&& dyn = DynamicEntry(dyns[i]);
        if (dyn.tag() == DT_NULL)
            break;
        if (func(dyn) == IterationDecision::Break)
            break;
    }
}
template<typename F>
inline void DynamicObject::for_each_needed_library(F func) const
{
    for_each_dynamic_entry([func, this](auto entry) {
        if (entry.tag() != DT_NEEDED)
            return IterationDecision::Continue;
        Elf32_Word offset = entry.val();
        const char* name = (const char*)(m_base_address.offset(m_string_table_offset).offset(offset)).as_ptr();
        if (func(StringView(name)) == IterationDecision::Break)
            return IterationDecision::Break;
        return IterationDecision::Continue;
    });
}

template<typename F>
void DynamicObject::for_each_initialization_array_function(F f) const
{
    if (!has_init_array_section())
        return;
    FlatPtr init_array = (FlatPtr)init_array_section().address().as_ptr();
    for (size_t i = 0; i < (m_init_array_size / sizeof(void*)); ++i) {
        InitializationFunction current = ((InitializationFunction*)(init_array))[i];
        f(current);
    }
}

} // end namespace ELF
