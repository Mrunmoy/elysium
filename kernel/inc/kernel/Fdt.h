// FDT (Flattened Device Tree) parser.
//
// Read-only parser for standard DTB binaries (magic 0xD00DFEED).
// Navigates the structure block to find nodes and properties.
// All values are stored big-endian in DTB; this parser converts
// to native (little-endian) on read.
//
// Thread safety: read-only, safe to call concurrently on same DTB.

#pragma once

#include <cstdint>

namespace kernel
{
namespace fdt
{
    struct Property
    {
        const char *name;
        const std::uint8_t *data;
        std::uint32_t size;
    };

    // Validate DTB header (magic, version, size).
    // Returns true if the DTB appears well-formed.
    bool validate(const std::uint8_t *dtb, std::uint32_t maxSize);

    // Find a node by path (e.g., "/clocks", "/console/tx").
    // Path "/" finds the root node.
    // Returns offset into the structure block, or -1 if not found.
    std::int32_t findNode(const std::uint8_t *dtb, const char *path);

    // Find a property within a node.
    // nodeOffset is the value returned by findNode().
    // Returns true and fills prop if found.
    bool findProperty(const std::uint8_t *dtb, std::int32_t nodeOffset,
                      const char *name, Property &prop);

    // Read a uint32 property (big-endian to native).
    bool readU32(const std::uint8_t *dtb, std::int32_t nodeOffset,
                 const char *name, std::uint32_t &value);

    // Read a string property (returns pointer into DTB blob).
    bool readString(const std::uint8_t *dtb, std::int32_t nodeOffset,
                    const char *name, const char *&value);

    // Check if a boolean (zero-length) property exists.
    bool hasProperty(const std::uint8_t *dtb, std::int32_t nodeOffset,
                     const char *name);

    // Read a reg property (base, size pair of uint32s).
    bool readReg(const std::uint8_t *dtb, std::int32_t nodeOffset,
                 std::uint32_t &base, std::uint32_t &size);

    // Get the first child node of a parent.
    // Returns offset or -1 if no children.
    std::int32_t firstChild(const std::uint8_t *dtb, std::int32_t parentOffset);

    // Get the next sibling node after the given node.
    // Returns offset or -1 if no more siblings.
    std::int32_t nextSibling(const std::uint8_t *dtb, std::int32_t nodeOffset);

    // Get the name of a node at the given offset.
    // Returns pointer into DTB blob.
    const char *nodeName(const std::uint8_t *dtb, std::int32_t nodeOffset);

}  // namespace fdt
}  // namespace kernel
