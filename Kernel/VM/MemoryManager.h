/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
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

#include <AK/HashTable.h>
#include <AK/NonnullRefPtrVector.h>
#include <AK/String.h>
#include <Kernel/Arch/i386/CPU.h>
#include <Kernel/Forward.h>
#include <Kernel/SpinLock.h>
#include <Kernel/VM/AllocationStrategy.h>
#include <Kernel/VM/PhysicalPage.h>
#include <Kernel/VM/Region.h>
#include <Kernel/VM/VMObject.h>

namespace Kernel {

#define PAGE_ROUND_UP(x) ((((FlatPtr)(x)) + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1)))
#define PAGE_ROUND_DOWN(x) (((FlatPtr)(x)) & ~(PAGE_SIZE - 1))

template<typename T>
inline T* low_physical_to_virtual(T* physical)
{
    return (T*)(((u8*)physical) + 0xc0000000);
}

inline u32 low_physical_to_virtual(u32 physical)
{
    return physical + 0xc0000000;
}

template<typename T>
inline T* virtual_to_low_physical(T* physical)
{
    return (T*)(((u8*)physical) - 0xc0000000);
}

inline u32 virtual_to_low_physical(u32 physical)
{
    return physical - 0xc0000000;
}

class KBuffer;
class SynthFSInode;

#define MM Kernel::MemoryManager::the()

struct MemoryManagerData {
    SpinLock<u8> m_quickmap_in_use;
    u32 m_quickmap_prev_flags;

    PhysicalAddress m_last_quickmap_pd;
    PhysicalAddress m_last_quickmap_pt;
};

extern RecursiveSpinLock s_mm_lock;

class MemoryManager {
    AK_MAKE_ETERNAL
    friend class PageDirectory;
    friend class PhysicalPage;
    friend class PhysicalRegion;
    friend class AnonymousVMObject;
    friend class Region;
    friend class VMObject;
    friend OwnPtr<KBuffer> procfs$memstat(InodeIdentifier);

public:
    static MemoryManager& the();
    static bool is_initialized();

    static void initialize(u32 cpu);

    static inline MemoryManagerData& get_data()
    {
        return Processor::current().get_mm_data();
    }

    PageFaultResponse handle_page_fault(const PageFault&);

    void enter_process_paging_scope(Process&);

    bool validate_user_stack(const Process&, VirtualAddress) const;

    enum class ShouldZeroFill {
        No,
        Yes
    };

    bool commit_user_physical_pages(size_t);
    void uncommit_user_physical_pages(size_t);
    NonnullRefPtr<PhysicalPage> allocate_committed_user_physical_page(ShouldZeroFill = ShouldZeroFill::Yes);
    RefPtr<PhysicalPage> allocate_user_physical_page(ShouldZeroFill = ShouldZeroFill::Yes, bool* did_purge = nullptr);
    RefPtr<PhysicalPage> allocate_supervisor_physical_page();
    NonnullRefPtrVector<PhysicalPage> allocate_contiguous_supervisor_physical_pages(size_t size);
    void deallocate_user_physical_page(const PhysicalPage&);
    void deallocate_supervisor_physical_page(const PhysicalPage&);

    OwnPtr<Region> allocate_contiguous_kernel_region(size_t, const StringView& name, u8 access, bool user_accessible = false, bool cacheable = true);
    OwnPtr<Region> allocate_kernel_region(size_t, const StringView& name, u8 access, bool user_accessible = false, AllocationStrategy strategy = AllocationStrategy::Reserve, bool cacheable = true);
    OwnPtr<Region> allocate_kernel_region(PhysicalAddress, size_t, const StringView& name, u8 access, bool user_accessible = false, bool cacheable = true);
    OwnPtr<Region> allocate_kernel_region_identity(PhysicalAddress, size_t, const StringView& name, u8 access, bool user_accessible = false, bool cacheable = true);
    OwnPtr<Region> allocate_kernel_region_with_vmobject(VMObject&, size_t, const StringView& name, u8 access, bool user_accessible = false, bool cacheable = true);
    OwnPtr<Region> allocate_kernel_region_with_vmobject(const Range&, VMObject&, const StringView& name, u8 access, bool user_accessible = false, bool cacheable = true);
    OwnPtr<Region> allocate_user_accessible_kernel_region(size_t, const StringView& name, u8 access, bool cacheable = true);

    unsigned user_physical_pages() const { return m_user_physical_pages; }
    unsigned user_physical_pages_used() const { return m_user_physical_pages_used; }
    unsigned user_physical_pages_committed() const { return m_user_physical_pages_committed; }
    unsigned user_physical_pages_uncommitted() const { return m_user_physical_pages_uncommitted; }
    unsigned super_physical_pages() const { return m_super_physical_pages; }
    unsigned super_physical_pages_used() const { return m_super_physical_pages_used; }

    template<typename Callback>
    static void for_each_vmobject(Callback callback)
    {
        for (auto& vmobject : MM.m_vmobjects) {
            if (callback(vmobject) == IterationDecision::Break)
                break;
        }
    }

    template<typename T, typename Callback>
    static void for_each_vmobject_of_type(Callback callback)
    {
        for (auto& vmobject : MM.m_vmobjects) {
            if (!is<T>(vmobject))
                continue;
            if (callback(static_cast<T&>(vmobject)) == IterationDecision::Break)
                break;
        }
    }

    static Region* find_region_from_vaddr(Process&, VirtualAddress);
    static const Region* find_region_from_vaddr(const Process&, VirtualAddress);

    void dump_kernel_regions();

    PhysicalPage& shared_zero_page() { return *m_shared_zero_page; }
    PhysicalPage& lazy_committed_page() { return *m_lazy_committed_page; }

    PageDirectory& kernel_page_directory() { return *m_kernel_page_directory; }

private:
    MemoryManager();
    ~MemoryManager();

    enum class AccessSpace { Kernel,
        User };
    enum class AccessType { Read,
        Write };
    template<AccessSpace, AccessType>
    bool validate_range(const Process&, VirtualAddress, size_t) const;

    void register_vmobject(VMObject&);
    void unregister_vmobject(VMObject&);
    void register_region(Region&);
    void unregister_region(Region&);

    void detect_cpu_features();
    void protect_kernel_image();
    void parse_memory_map();
    static void flush_tlb_local(VirtualAddress, size_t page_count = 1);
    static void flush_tlb(const PageDirectory*, VirtualAddress, size_t page_count = 1);

    static Region* user_region_from_vaddr(Process&, VirtualAddress);
    static Region* kernel_region_from_vaddr(VirtualAddress);

    static Region* find_region_from_vaddr(VirtualAddress);

    RefPtr<PhysicalPage> find_free_user_physical_page(bool);
    u8* quickmap_page(PhysicalPage&);
    void unquickmap_page();

    PageDirectoryEntry* quickmap_pd(PageDirectory&, size_t pdpt_index);
    PageTableEntry* quickmap_pt(PhysicalAddress);

    PageTableEntry* pte(PageDirectory&, VirtualAddress);
    PageTableEntry* ensure_pte(PageDirectory&, VirtualAddress);
    void release_pte(PageDirectory&, VirtualAddress, bool);

    RefPtr<PageDirectory> m_kernel_page_directory;
    RefPtr<PhysicalPage> m_low_page_table;

    RefPtr<PhysicalPage> m_shared_zero_page;
    RefPtr<PhysicalPage> m_lazy_committed_page;

    Atomic<unsigned, AK::MemoryOrder::memory_order_relaxed> m_user_physical_pages { 0 };
    Atomic<unsigned, AK::MemoryOrder::memory_order_relaxed> m_user_physical_pages_used { 0 };
    Atomic<unsigned, AK::MemoryOrder::memory_order_relaxed> m_user_physical_pages_committed { 0 };
    Atomic<unsigned, AK::MemoryOrder::memory_order_relaxed> m_user_physical_pages_uncommitted { 0 };
    Atomic<unsigned, AK::MemoryOrder::memory_order_relaxed> m_super_physical_pages { 0 };
    Atomic<unsigned, AK::MemoryOrder::memory_order_relaxed> m_super_physical_pages_used { 0 };

    NonnullRefPtrVector<PhysicalRegion> m_user_physical_regions;
    NonnullRefPtrVector<PhysicalRegion> m_super_physical_regions;

    InlineLinkedList<Region> m_user_regions;
    InlineLinkedList<Region> m_kernel_regions;

    InlineLinkedList<VMObject> m_vmobjects;

    RefPtr<PhysicalPage> m_low_pseudo_identity_mapping_pages[4];
};

template<typename Callback>
void VMObject::for_each_region(Callback callback)
{
    ScopedSpinLock lock(s_mm_lock);
    // FIXME: Figure out a better data structure so we don't have to walk every single region every time an inode changes.
    //        Perhaps VMObject could have a Vector<Region*> with all of his mappers?
    for (auto& region : MM.m_user_regions) {
        if (&region.vmobject() == this)
            callback(region);
    }
    for (auto& region : MM.m_kernel_regions) {
        if (&region.vmobject() == this)
            callback(region);
    }
}

inline bool is_user_address(VirtualAddress vaddr)
{
    return vaddr.get() < 0xc0000000;
}

inline bool is_user_range(VirtualAddress vaddr, size_t size)
{
    if (vaddr.offset(size) < vaddr)
        return false;
    return is_user_address(vaddr) && is_user_address(vaddr.offset(size));
}

inline bool PhysicalPage::is_shared_zero_page() const
{
    return this == &MM.shared_zero_page();
}

inline bool PhysicalPage::is_lazy_committed_page() const
{
    return this == &MM.lazy_committed_page();
}

}
