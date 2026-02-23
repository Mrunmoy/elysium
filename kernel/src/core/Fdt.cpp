// FDT parser implementation.
//
// Read-only parser for standard DTB binaries.  All multi-byte integers
// in the DTB are big-endian; we convert to native with __builtin_bswap32.

#include "kernel/Fdt.h"

#include <cstring>

namespace kernel
{
namespace fdt
{
namespace
{
    // FDT token values
    static constexpr std::uint32_t kFdtMagic = 0xD00DFEED;
    static constexpr std::uint32_t kFdtBeginNode = 1;
    static constexpr std::uint32_t kFdtEndNode = 2;
    static constexpr std::uint32_t kFdtProp = 3;
    static constexpr std::uint32_t kFdtNop = 4;
    static constexpr std::uint32_t kFdtEnd = 9;

    static constexpr std::uint32_t kHeaderSize = 40;

    inline std::uint32_t be32(const std::uint8_t *p)
    {
        return __builtin_bswap32(*reinterpret_cast<const std::uint32_t *>(p));
    }

    inline std::uint32_t align4(std::uint32_t v)
    {
        return (v + 3u) & ~3u;
    }

    // Header accessors
    std::uint32_t hdrTotalSize(const std::uint8_t *dtb)    { return be32(dtb + 4); }
    std::uint32_t hdrOffStruct(const std::uint8_t *dtb)    { return be32(dtb + 8); }
    std::uint32_t hdrOffStrings(const std::uint8_t *dtb)   { return be32(dtb + 12); }
    std::uint32_t hdrSizeStruct(const std::uint8_t *dtb)   { return be32(dtb + 36); }

    // Get string from strings block by offset
    const char *getString(const std::uint8_t *dtb, std::uint32_t nameOff)
    {
        std::uint32_t stringsOff = hdrOffStrings(dtb);
        return reinterpret_cast<const char *>(dtb + stringsOff + nameOff);
    }

    // Read a token at a structure block offset.
    // Returns the token value, advances pos past the token word.
    std::uint32_t readToken(const std::uint8_t *dtb, std::uint32_t &pos)
    {
        std::uint32_t structBase = hdrOffStruct(dtb);
        std::uint32_t tok = be32(dtb + structBase + pos);
        pos += 4;
        return tok;
    }

    // Skip past a node name at current position.
    // Advances pos past the null-terminated name and alignment padding.
    void skipNodeName(const std::uint8_t *dtb, std::uint32_t &pos)
    {
        std::uint32_t structBase = hdrOffStruct(dtb);
        const char *name = reinterpret_cast<const char *>(dtb + structBase + pos);
        std::uint32_t nameLen = static_cast<std::uint32_t>(std::strlen(name)) + 1;
        pos += align4(nameLen);
    }

    // Skip an entire subtree starting from just after FDT_BEGIN_NODE + name.
    // After return, pos points just after the matching FDT_END_NODE.
    void skipNode(const std::uint8_t *dtb, std::uint32_t &pos)
    {
        int depth = 1;
        std::uint32_t structSize = hdrSizeStruct(dtb);

        while (depth > 0 && pos < structSize)
        {
            std::uint32_t tok = readToken(dtb, pos);
            switch (tok)
            {
            case kFdtBeginNode:
                skipNodeName(dtb, pos);
                ++depth;
                break;
            case kFdtEndNode:
                --depth;
                break;
            case kFdtProp:
            {
                std::uint32_t len = readToken(dtb, pos);  // property len
                pos += 4;  // skip nameoff
                pos += align4(len);
                break;
            }
            case kFdtNop:
                break;
            case kFdtEnd:
                return;
            default:
                return;
            }
        }
    }

    // Find a direct child node of the node at parentPos.
    // parentPos must point just after the FDT_BEGIN_NODE token + node name.
    // Returns the position of the matching FDT_BEGIN_NODE name start, or -1.
    std::int32_t findDirectChild(const std::uint8_t *dtb, std::uint32_t parentPos,
                                 const char *childName)
    {
        std::uint32_t pos = parentPos;
        std::uint32_t structBase = hdrOffStruct(dtb);
        std::uint32_t structSize = hdrSizeStruct(dtb);

        while (pos < structSize)
        {
            std::uint32_t tok = readToken(dtb, pos);
            switch (tok)
            {
            case kFdtBeginNode:
            {
                // pos now points to the node name
                const char *name = reinterpret_cast<const char *>(
                    dtb + structBase + pos);
                std::uint32_t savedPos = pos;
                skipNodeName(dtb, pos);

                if (std::strcmp(name, childName) == 0)
                {
                    // Return the position of the name start
                    return static_cast<std::int32_t>(savedPos);
                }
                // Not the one -- skip its entire subtree
                skipNode(dtb, pos);
                break;
            }
            case kFdtProp:
            {
                std::uint32_t len = readToken(dtb, pos);
                pos += 4;  // nameoff
                pos += align4(len);
                break;
            }
            case kFdtEndNode:
                // End of parent node -- child not found
                return -1;
            case kFdtNop:
                break;
            case kFdtEnd:
                return -1;
            default:
                return -1;
            }
        }
        return -1;
    }

}  // namespace

bool validate(const std::uint8_t *dtb, std::uint32_t maxSize)
{
    if (dtb == nullptr || maxSize < kHeaderSize)
    {
        return false;
    }

    if (be32(dtb) != kFdtMagic)
    {
        return false;
    }

    std::uint32_t totalSize = hdrTotalSize(dtb);
    if (totalSize > maxSize)
    {
        return false;
    }

    return true;
}

std::int32_t findNode(const std::uint8_t *dtb, const char *path)
{
    if (path == nullptr || path[0] != '/')
    {
        return -1;
    }

    // Start at the root node
    std::uint32_t pos = 0;
    std::uint32_t tok = readToken(dtb, pos);
    if (tok != kFdtBeginNode)
    {
        return -1;
    }

    // Root node name position
    std::uint32_t rootNamePos = pos;
    skipNodeName(dtb, pos);

    // If path is just "/", return the root
    if (path[1] == '\0')
    {
        return static_cast<std::int32_t>(rootNamePos);
    }

    // Walk path components: "/board" -> "board", "/console/tx" -> "console" then "tx"
    const char *p = path + 1;  // skip leading '/'
    std::uint32_t searchPos = pos;  // after root name

    while (*p != '\0')
    {
        // Extract next path component
        const char *slash = p;
        while (*slash != '\0' && *slash != '/')
        {
            ++slash;
        }

        char component[64];
        std::uint32_t len = static_cast<std::uint32_t>(slash - p);
        if (len >= sizeof(component))
        {
            return -1;
        }
        std::memcpy(component, p, len);
        component[len] = '\0';

        std::int32_t childOff = findDirectChild(dtb, searchPos, component);
        if (childOff < 0)
        {
            return -1;
        }

        // childOff points to the name start -- advance past it for deeper searches
        std::uint32_t childPos = static_cast<std::uint32_t>(childOff);
        skipNodeName(dtb, childPos);

        p = slash;
        if (*p == '/')
        {
            ++p;
        }

        if (*p == '\0')
        {
            // Found the target node; return the name position
            return childOff;
        }

        // Continue search within this child
        searchPos = childPos;
    }

    return -1;
}

bool findProperty(const std::uint8_t *dtb, std::int32_t nodeOffset,
                  const char *name, Property &prop)
{
    if (nodeOffset < 0)
    {
        return false;
    }

    std::uint32_t pos = static_cast<std::uint32_t>(nodeOffset);
    std::uint32_t structBase = hdrOffStruct(dtb);

    // Skip the node name
    skipNodeName(dtb, pos);

    // Scan properties until we hit FDT_BEGIN_NODE or FDT_END_NODE
    std::uint32_t structSize = hdrSizeStruct(dtb);
    while (pos < structSize)
    {
        std::uint32_t tok = readToken(dtb, pos);
        switch (tok)
        {
        case kFdtProp:
        {
            std::uint32_t len = readToken(dtb, pos);
            std::uint32_t nameOff = readToken(dtb, pos);
            const char *propName = getString(dtb, nameOff);
            const std::uint8_t *data = dtb + structBase + pos;

            if (std::strcmp(propName, name) == 0)
            {
                prop.name = propName;
                prop.data = data;
                prop.size = len;
                return true;
            }

            pos += align4(len);
            break;
        }
        case kFdtBeginNode:
        case kFdtEndNode:
        case kFdtEnd:
            // Past properties of this node
            return false;
        case kFdtNop:
            break;
        default:
            return false;
        }
    }
    return false;
}

bool readU32(const std::uint8_t *dtb, std::int32_t nodeOffset,
             const char *name, std::uint32_t &value)
{
    Property prop{};
    if (!findProperty(dtb, nodeOffset, name, prop))
    {
        return false;
    }
    if (prop.size != 4)
    {
        return false;
    }
    value = be32(prop.data);
    return true;
}

bool readString(const std::uint8_t *dtb, std::int32_t nodeOffset,
                const char *name, const char *&value)
{
    Property prop{};
    if (!findProperty(dtb, nodeOffset, name, prop))
    {
        return false;
    }
    if (prop.size == 0)
    {
        return false;
    }
    value = reinterpret_cast<const char *>(prop.data);
    return true;
}

bool hasProperty(const std::uint8_t *dtb, std::int32_t nodeOffset,
                 const char *name)
{
    Property prop{};
    return findProperty(dtb, nodeOffset, name, prop);
}

bool readReg(const std::uint8_t *dtb, std::int32_t nodeOffset,
             std::uint32_t &base, std::uint32_t &size)
{
    Property prop{};
    if (!findProperty(dtb, nodeOffset, "reg", prop))
    {
        return false;
    }
    if (prop.size != 8)
    {
        return false;
    }
    base = be32(prop.data);
    size = be32(prop.data + 4);
    return true;
}

std::int32_t firstChild(const std::uint8_t *dtb, std::int32_t parentOffset)
{
    if (parentOffset < 0)
    {
        return -1;
    }

    std::uint32_t pos = static_cast<std::uint32_t>(parentOffset);

    // Skip the node name
    skipNodeName(dtb, pos);

    // Scan for the first FDT_BEGIN_NODE (child)
    std::uint32_t structSize = hdrSizeStruct(dtb);
    while (pos < structSize)
    {
        std::uint32_t tok = readToken(dtb, pos);
        switch (tok)
        {
        case kFdtBeginNode:
            // pos now points to the child name
            return static_cast<std::int32_t>(pos);
        case kFdtProp:
        {
            std::uint32_t len = readToken(dtb, pos);
            pos += 4;  // nameoff
            pos += align4(len);
            break;
        }
        case kFdtEndNode:
            return -1;
        case kFdtNop:
            break;
        case kFdtEnd:
            return -1;
        default:
            return -1;
        }
    }
    return -1;
}

std::int32_t nextSibling(const std::uint8_t *dtb, std::int32_t nodeOffset)
{
    if (nodeOffset < 0)
    {
        return -1;
    }

    std::uint32_t pos = static_cast<std::uint32_t>(nodeOffset);

    // Skip this node name
    skipNodeName(dtb, pos);

    // Skip the entire subtree of this node
    skipNode(dtb, pos);

    // Now we should be right after FDT_END_NODE of this node.
    // Scan for the next FDT_BEGIN_NODE (sibling).
    std::uint32_t structSize = hdrSizeStruct(dtb);
    while (pos < structSize)
    {
        std::uint32_t tok = readToken(dtb, pos);
        switch (tok)
        {
        case kFdtBeginNode:
            return static_cast<std::int32_t>(pos);
        case kFdtProp:
        {
            std::uint32_t len = readToken(dtb, pos);
            pos += 4;
            pos += align4(len);
            break;
        }
        case kFdtEndNode:
            // End of parent -- no more siblings
            return -1;
        case kFdtNop:
            break;
        case kFdtEnd:
            return -1;
        default:
            return -1;
        }
    }
    return -1;
}

const char *nodeName(const std::uint8_t *dtb, std::int32_t nodeOffset)
{
    if (nodeOffset < 0)
    {
        return nullptr;
    }

    std::uint32_t structBase = hdrOffStruct(dtb);
    return reinterpret_cast<const char *>(
        dtb + structBase + static_cast<std::uint32_t>(nodeOffset));
}

}  // namespace fdt
}  // namespace kernel
