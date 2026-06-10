#!/usr/bin/env python3
import argparse
import os
from dataclasses import dataclass, field
from pathlib import Path


EMPTY = 0xFFFFFFFF
HEADER_SIZE = 0x50
FILE_PARTITION_OFS = 0x200


def align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def read_u32(data, offset):
    return int.from_bytes(data[offset:offset + 4], "big")


def read_u64(data, offset):
    return int.from_bytes(data[offset:offset + 8], "big")


def write_u32(data, offset, value):
    data[offset:offset + 4] = int(value).to_bytes(4, "big")


def write_u64(data, offset, value):
    data[offset:offset + 8] = int(value).to_bytes(8, "big")


def get_hash_table_count(num_entries):
    if num_entries < 3:
        return 3
    if num_entries < 19:
        return num_entries | 1

    count = num_entries
    while any(count % divisor == 0 for divisor in (2, 3, 5, 7, 11, 13, 17)):
        count += 1
    return count


def normalize_char(value):
    if ord("a") <= value <= ord("z"):
        return value + ord("A") - ord("a")
    return value


def calc_path_hash(parent, name_bytes):
    value = parent ^ 123456789
    for byte in name_bytes:
        value = ((value >> 5) | ((value << 27) & 0xFFFFFFFF)) & 0xFFFFFFFF
        value ^= normalize_char(byte)
    return value & 0xFFFFFFFF


@dataclass
class Header:
    dir_hash_ofs: int
    dir_hash_size: int
    dir_table_ofs: int
    dir_table_size: int
    file_hash_ofs: int
    file_hash_size: int
    file_table_ofs: int
    file_table_size: int
    file_partition_ofs: int


@dataclass
class RawDir:
    parent: int
    sibling: int
    child: int
    file: int
    name: str


@dataclass
class RawFile:
    parent: int
    sibling: int
    offset: int
    size: int
    name: str


@dataclass
class Node:
    name: str
    is_dir: bool
    data: bytes = b""
    children: list = field(default_factory=list)
    parent: object = None
    entry_offset: int = 0
    file_offset: int = 0
    sibling: object = None
    dir_child: object = None
    file_child: object = None

    def add_child(self, child):
        child.parent = self
        self.children.append(child)
        return child


@dataclass
class BuildContext:
    num_dirs: int = 1
    num_files: int = 0
    dir_table_size: int = 0x18
    file_table_size: int = 0
    dir_hash_table_size: int = 0
    file_hash_table_size: int = 0
    file_partition_size: int = 0


def parse_header(data):
    if len(data) < HEADER_SIZE:
        raise ValueError("WUHB is too small")
    if data[:4] != b"WUHB":
        raise ValueError("missing WUHB magic")
    if read_u32(data, 4) != HEADER_SIZE:
        raise ValueError("unexpected WUHB header size")

    header = Header(
        dir_hash_ofs=read_u64(data, 8),
        dir_hash_size=read_u64(data, 16),
        dir_table_ofs=read_u64(data, 24),
        dir_table_size=read_u64(data, 32),
        file_hash_ofs=read_u64(data, 40),
        file_hash_size=read_u64(data, 48),
        file_table_ofs=read_u64(data, 56),
        file_table_size=read_u64(data, 64),
        file_partition_ofs=read_u64(data, 72),
    )
    if header.file_partition_ofs != FILE_PARTITION_OFS:
        raise ValueError(f"unexpected WUHB file partition offset 0x{header.file_partition_ofs:x}")
    return header


def parse_dir_table(data, header):
    entries = {}
    offset = 0
    table_end = header.dir_table_ofs + header.dir_table_size
    while header.dir_table_ofs + offset + 0x18 <= table_end:
        entry_offset = header.dir_table_ofs + offset
        name_size = read_u32(data, entry_offset + 20)
        entry_size = 0x18 + align(name_size, 4)
        if entry_offset + entry_size > table_end:
            raise ValueError("directory table entry exceeds table size")

        name_start = entry_offset + 0x18
        name = data[name_start:name_start + name_size].decode("utf-8")
        entries[offset] = RawDir(
            parent=read_u32(data, entry_offset),
            sibling=read_u32(data, entry_offset + 4),
            child=read_u32(data, entry_offset + 8),
            file=read_u32(data, entry_offset + 12),
            name=name,
        )
        offset += entry_size
    return entries


def parse_file_table(data, header):
    entries = {}
    offset = 0
    table_end = header.file_table_ofs + header.file_table_size
    while header.file_table_ofs + offset + 0x20 <= table_end:
        entry_offset = header.file_table_ofs + offset
        name_size = read_u32(data, entry_offset + 28)
        entry_size = 0x20 + align(name_size, 4)
        if entry_offset + entry_size > table_end:
            raise ValueError("file table entry exceeds table size")

        name_start = entry_offset + 0x20
        name = data[name_start:name_start + name_size].decode("utf-8")
        entries[offset] = RawFile(
            parent=read_u32(data, entry_offset),
            sibling=read_u32(data, entry_offset + 4),
            offset=read_u64(data, entry_offset + 8),
            size=read_u64(data, entry_offset + 16),
            name=name,
        )
        offset += entry_size
    return entries


def parse_wuhb_tree(data):
    header = parse_header(data)
    raw_dirs = parse_dir_table(data, header)
    raw_files = parse_file_table(data, header)

    if 0 not in raw_dirs:
        raise ValueError("WUHB has no root directory")

    visited_dirs = set()

    def build_dir(offset):
        if offset == EMPTY:
            return None
        if offset in visited_dirs:
            raise ValueError("directory cycle in WUHB")
        if offset not in raw_dirs:
            raise ValueError(f"directory offset 0x{offset:x} is missing")

        visited_dirs.add(offset)
        raw = raw_dirs[offset]
        node = Node(raw.name, True)

        child_offset = raw.child
        visited_siblings = set()
        while child_offset != EMPTY:
            if child_offset in visited_siblings:
                raise ValueError("directory sibling cycle in WUHB")
            visited_siblings.add(child_offset)
            child = build_dir(child_offset)
            node.add_child(child)
            child_offset = raw_dirs[child_offset].sibling

        file_offset = raw.file
        visited_file_siblings = set()
        while file_offset != EMPTY:
            if file_offset in visited_file_siblings:
                raise ValueError("file sibling cycle in WUHB")
            visited_file_siblings.add(file_offset)
            if file_offset not in raw_files:
                raise ValueError(f"file offset 0x{file_offset:x} is missing")

            raw_file = raw_files[file_offset]
            start = header.file_partition_ofs + raw_file.offset
            end = start + raw_file.size
            if end > len(data):
                raise ValueError(f"file {raw_file.name} extends past end of WUHB")

            node.add_child(Node(raw_file.name, False, data=bytes(data[start:end])))
            file_offset = raw_file.sibling

        return node

    return build_dir(0)


def find_child_dir(parent, name):
    for child in parent.children:
        if child.is_dir and child.name == name:
            return child
    return None


def inject_boot_sound(root, sound_data, entry_name):
    meta = find_child_dir(root, "meta")
    if meta is None:
        meta = root.add_child(Node("meta", True))

    for child in meta.children:
        if not child.is_dir and child.name == entry_name:
            child.data = sound_data
            return "replaced"

    meta.add_child(Node(entry_name, False, data=sound_data))
    return "added"


def fill_info(node, ctx):
    if node.is_dir:
        ctx.num_dirs += 1
        ctx.dir_table_size += 0x18 + align(len(node.name.encode("utf-8")), 4)
        for child in node.children:
            fill_info(child, ctx)
    else:
        ctx.num_files += 1
        ctx.file_table_size += 0x20 + align(len(node.name.encode("utf-8")), 4)


def calculate_dir_offsets(node, next_offset):
    node.entry_offset = next_offset
    next_offset += 0x18 + align(len(node.name.encode("utf-8")), 4)
    for child in node.children:
        if child.is_dir:
            next_offset = calculate_dir_offsets(child, next_offset)
    return next_offset


def calculate_file_offsets(node, ctx, next_offset):
    for child in node.children:
        if child.is_dir:
            next_offset = calculate_file_offsets(child, ctx, next_offset)
    for child in node.children:
        if not child.is_dir:
            ctx.file_partition_size = align(ctx.file_partition_size, 0x10)
            child.file_offset = ctx.file_partition_size
            child.entry_offset = next_offset
            ctx.file_partition_size += len(child.data)
            next_offset += 0x20 + align(len(child.name.encode("utf-8")), 4)
    return next_offset


def update_siblings(node):
    dir_children = [child for child in node.children if child.is_dir]
    file_children = [child for child in node.children if not child.is_dir]

    node.dir_child = dir_children[0] if dir_children else None
    node.file_child = file_children[0] if file_children else None

    for children in (dir_children, file_children):
        for index, child in enumerate(children):
            child.sibling = children[index + 1] if index + 1 < len(children) else None

    for child in dir_children:
        update_siblings(child)


def write_dir_entry(table, hash_table, hash_count, node):
    offset = node.entry_offset
    name = node.name.encode("utf-8")
    parent_offset = node.entry_offset if node.parent is None else node.parent.entry_offset
    bucket = calc_path_hash(0 if node.parent is None else node.parent.entry_offset, name) % hash_count

    write_u32(table, offset, parent_offset)
    write_u32(table, offset + 4, EMPTY if node.sibling is None else node.sibling.entry_offset)
    write_u32(table, offset + 8, EMPTY if node.dir_child is None else node.dir_child.entry_offset)
    write_u32(table, offset + 12, EMPTY if node.file_child is None else node.file_child.entry_offset)
    write_u32(table, offset + 16, hash_table[bucket])
    hash_table[bucket] = node.entry_offset
    write_u32(table, offset + 20, len(name))
    table[offset + 0x18:offset + 0x18 + len(name)] = name


def write_file_entry(table, hash_table, hash_count, node):
    offset = node.entry_offset
    name = node.name.encode("utf-8")
    bucket = calc_path_hash(node.parent.entry_offset, name) % hash_count

    write_u32(table, offset, node.parent.entry_offset)
    write_u32(table, offset + 4, EMPTY if node.sibling is None else node.sibling.entry_offset)
    write_u64(table, offset + 8, node.file_offset)
    write_u64(table, offset + 16, len(node.data))
    write_u32(table, offset + 24, hash_table[bucket])
    hash_table[bucket] = node.entry_offset
    write_u32(table, offset + 28, len(name))
    table[offset + 0x20:offset + 0x20 + len(name)] = name


def populate_tables(root, ctx):
    dir_hash_count = ctx.dir_hash_table_size // 4
    file_hash_count = ctx.file_hash_table_size // 4
    dir_hash = [EMPTY] * dir_hash_count
    file_hash = [EMPTY] * file_hash_count
    dir_table = bytearray(ctx.dir_table_size)
    file_table = bytearray(ctx.file_table_size)

    def visit_dirs(node):
        write_dir_entry(dir_table, dir_hash, dir_hash_count, node)
        for child in node.children:
            if child.is_dir:
                visit_dirs(child)

    def visit_files(node):
        for child in node.children:
            if child.is_dir:
                visit_files(child)
        for child in node.children:
            if not child.is_dir:
                write_file_entry(file_table, file_hash, file_hash_count, child)

    visit_dirs(root)
    visit_files(root)

    dir_hash_table = bytearray()
    for value in dir_hash:
        dir_hash_table.extend(value.to_bytes(4, "big"))

    file_hash_table = bytearray()
    for value in file_hash:
        file_hash_table.extend(value.to_bytes(4, "big"))

    return dir_hash_table, dir_table, file_hash_table, file_table


def iter_files(node):
    for child in node.children:
        if child.is_dir:
            yield from iter_files(child)
    for child in node.children:
        if not child.is_dir:
            yield child


def build_wuhb(root):
    ctx = BuildContext()
    fill_info(root, ctx)
    ctx.dir_hash_table_size = 4 * get_hash_table_count(ctx.num_dirs)
    ctx.file_hash_table_size = 4 * get_hash_table_count(ctx.num_files)

    calculate_dir_offsets(root, 0)
    calculate_file_offsets(root, ctx, 0)
    update_siblings(root)

    dir_hash_table, dir_table, file_hash_table, file_table = populate_tables(root, ctx)

    dir_hash_ofs = align(ctx.file_partition_size + FILE_PARTITION_OFS, 4)
    dir_table_ofs = dir_hash_ofs + len(dir_hash_table)
    file_hash_ofs = dir_table_ofs + len(dir_table)
    file_table_ofs = file_hash_ofs + len(file_hash_table)
    output_size = file_table_ofs + len(file_table)
    output = bytearray(output_size)

    output[0:4] = b"WUHB"
    write_u32(output, 4, HEADER_SIZE)
    write_u64(output, 8, dir_hash_ofs)
    write_u64(output, 16, len(dir_hash_table))
    write_u64(output, 24, dir_table_ofs)
    write_u64(output, 32, len(dir_table))
    write_u64(output, 40, file_hash_ofs)
    write_u64(output, 48, len(file_hash_table))
    write_u64(output, 56, file_table_ofs)
    write_u64(output, 64, len(file_table))
    write_u64(output, 72, FILE_PARTITION_OFS)

    for node in iter_files(root):
        start = FILE_PARTITION_OFS + node.file_offset
        output[start:start + len(node.data)] = node.data

    output[dir_hash_ofs:dir_hash_ofs + len(dir_hash_table)] = dir_hash_table
    output[dir_table_ofs:dir_table_ofs + len(dir_table)] = dir_table
    output[file_hash_ofs:file_hash_ofs + len(file_hash_table)] = file_hash_table
    output[file_table_ofs:file_table_ofs + len(file_table)] = file_table
    return bytes(output), ctx.num_files


def write_atomic(path, data):
    tmp_path = path.with_name(path.name + ".tmp")
    tmp_path.write_bytes(data)
    os.replace(tmp_path, path)


def main():
    parser = argparse.ArgumentParser(description="Inject Wii U bootSound.btsnd into a WUHB RomFS bundle.")
    parser.add_argument("wuhb", type=Path)
    parser.add_argument("boot_sound", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--entry-name", default="bootSound.btsnd")
    args = parser.parse_args()

    wuhb_path = args.wuhb
    output_path = args.output or wuhb_path
    sound_data = args.boot_sound.read_bytes()

    root = parse_wuhb_tree(wuhb_path.read_bytes())
    action = inject_boot_sound(root, sound_data, args.entry_name)
    output, file_count = build_wuhb(root)
    write_atomic(output_path, output)

    print(f"{action} meta/{args.entry_name}: {len(sound_data)} bytes; {file_count} files; {output_path}")


if __name__ == "__main__":
    main()
