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

#include <AK/HashMap.h>
#include <AK/Types.h>
#include <Kernel/FileSystem/FileSystem.h>
#include <Kernel/FileSystem/Inode.h>
#include <Kernel/KBufferBuilder.h>
#include <Kernel/Lock.h>

namespace Kernel {

class Process;

class ProcFSInode;

class ProcFS final : public FS {
    friend class ProcFSInode;

public:
    virtual ~ProcFS() override;
    static NonnullRefPtr<ProcFS> create();

    virtual bool initialize() override;
    virtual const char* class_name() const override;

    virtual NonnullRefPtr<Inode> root_inode() const override;

    static void add_sys_bool(String&&, Lockable<bool>&, Function<void()>&& notify_callback = nullptr);
    static void add_sys_string(String&&, Lockable<String>&, Function<void()>&& notify_callback = nullptr);

private:
    ProcFS();

    struct ProcFSDirectoryEntry {
        ProcFSDirectoryEntry() { }
        ProcFSDirectoryEntry(const char* a_name, unsigned a_proc_file_type, bool a_supervisor_only, bool (*read_callback)(InodeIdentifier, KBufferBuilder&) = nullptr, ssize_t (*write_callback)(InodeIdentifier, const UserOrKernelBuffer&, size_t) = nullptr, RefPtr<ProcFSInode>&& a_inode = nullptr)
            : name(a_name)
            , proc_file_type(a_proc_file_type)
            , supervisor_only(a_supervisor_only)
            , read_callback(read_callback)
            , write_callback(write_callback)
            , inode(move(a_inode))
        {
        }

        const char* name { nullptr };
        unsigned proc_file_type { 0 };
        bool supervisor_only { false };
        bool (*read_callback)(InodeIdentifier, KBufferBuilder&);
        ssize_t (*write_callback)(InodeIdentifier, const UserOrKernelBuffer&, size_t);
        RefPtr<ProcFSInode> inode;
        InodeIdentifier identifier(unsigned fsid) const;
    };

    RefPtr<Inode> get_inode(InodeIdentifier) const;
    ProcFSDirectoryEntry* get_directory_entry(InodeIdentifier) const;

    Vector<ProcFSDirectoryEntry> m_entries;

    mutable Lock m_inodes_lock;
    mutable HashMap<unsigned, ProcFSInode*> m_inodes;
    RefPtr<ProcFSInode> m_root_inode;
};

class ProcFSInode final : public Inode {
    friend class ProcFS;

public:
    virtual ~ProcFSInode() override;

private:
    // ^Inode
    virtual KResult attach(FileDescription&) override;
    virtual void did_seek(FileDescription&, off_t) override;
    virtual ssize_t read_bytes(off_t, ssize_t, UserOrKernelBuffer& buffer, FileDescription*) const override;
    virtual InodeMetadata metadata() const override;
    virtual KResult traverse_as_directory(Function<bool(const FS::DirectoryEntryView&)>) const override;
    virtual RefPtr<Inode> lookup(StringView name) override;
    virtual void flush_metadata() override;
    virtual ssize_t write_bytes(off_t, ssize_t, const UserOrKernelBuffer& buffer, FileDescription*) override;
    virtual KResultOr<NonnullRefPtr<Inode>> create_child(const String& name, mode_t, dev_t, uid_t, gid_t) override;
    virtual KResult add_child(Inode&, const StringView& name, mode_t) override;
    virtual KResult remove_child(const StringView& name) override;
    virtual KResultOr<size_t> directory_entry_count() const override;
    virtual KResult chmod(mode_t) override;
    virtual KResult chown(uid_t, gid_t) override;
    virtual KResultOr<NonnullRefPtr<Custody>> resolve_as_link(Custody& base, RefPtr<Custody>* out_parent = nullptr, int options = 0, int symlink_recursion_level = 0) const override;

    KResult refresh_data(FileDescription&) const;

    ProcFS& fs() { return static_cast<ProcFS&>(Inode::fs()); }
    const ProcFS& fs() const { return static_cast<const ProcFS&>(Inode::fs()); }
    ProcFSInode(ProcFS&, unsigned index);
};

class ProcFSProxyInode final : public Inode {
    friend class ProcFSInode;

public:
    virtual ~ProcFSProxyInode() override;

private:
    // ^Inode
    virtual KResult attach(FileDescription&) override;
    virtual void did_seek(FileDescription&, off_t) override;
    virtual ssize_t read_bytes(off_t, ssize_t, UserOrKernelBuffer&, FileDescription*) const override { ASSERT_NOT_REACHED(); }
    virtual InodeMetadata metadata() const override;
    virtual KResult traverse_as_directory(Function<bool(const FS::DirectoryEntryView&)>) const override { ASSERT_NOT_REACHED(); }
    virtual RefPtr<Inode> lookup(StringView name) override;
    virtual void flush_metadata() override {};
    virtual ssize_t write_bytes(off_t, ssize_t, const UserOrKernelBuffer&, FileDescription*) override { ASSERT_NOT_REACHED(); }
    virtual KResultOr<NonnullRefPtr<Inode>> create_child(const String& name, mode_t, dev_t, uid_t, gid_t) override;
    virtual KResult add_child(Inode&, const StringView& name, mode_t) override;
    virtual KResult remove_child(const StringView& name) override;
    virtual KResultOr<size_t> directory_entry_count() const override;
    virtual KResult chmod(mode_t) override { return KResult(-EINVAL); }
    virtual KResult chown(uid_t, gid_t) override { return KResult(-EINVAL); }
    virtual KResultOr<NonnullRefPtr<Custody>> resolve_as_link(Custody&, RefPtr<Custody>*, int, int) const override { ASSERT_NOT_REACHED(); }
    virtual FileDescription* preopen_fd() override { return m_fd; }

    ProcFS& fs() { return static_cast<ProcFS&>(Inode::fs()); }
    const ProcFS& fs() const { return static_cast<const ProcFS&>(Inode::fs()); }

    ProcFSProxyInode(ProcFS&, FileDescription&);
    static NonnullRefPtr<ProcFSProxyInode> create(ProcFS& fs, FileDescription& fd)
    {
        return adopt(*new ProcFSProxyInode(fs, fd));
    }

    NonnullRefPtr<FileDescription> m_fd;
};

}
