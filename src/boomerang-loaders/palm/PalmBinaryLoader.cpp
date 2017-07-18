/*
 * Copyright (C) 2000, The University of Queensland
 * Copyright (C) 2001, Sun Microsystems, Inc
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 *
 */

/** \file PalmBinaryLoader.cpp
 * This class loads a Palm Pilot .prc file.
 * Derived from class IFileLoader.
 */

#include "PalmBinaryLoader.h"
#include "palmsystraps.h"

#include "boomerang/core/IBoomerang.h"
#include "boomerang/db/IBinaryImage.h"
#include "boomerang/db/IBinarySymbols.h"
#include "boomerang/db/IBinarySection.h"

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <QDebug>


// Macro to convert a pointer to a Big Endian integer into a host integer
#define UC(ad)      ((unsigned char *)ad)
#define UINT4(p)    ((UC((p))[0] << 24) + (UC(p)[1] << 16) + (UC(p)[2] << 8) + UC(p)[3])
#define UINT4ADDR(p) \
    ((UC((p).value())[0] << 24) + (UC((p).value())[1] << 16) + (UC((p).value())[2] << 8) + UC((p).value())[3])


PalmBinaryLoader::PalmBinaryLoader()
    : m_pImage(nullptr)
    , m_pData(nullptr)
{
}


PalmBinaryLoader::~PalmBinaryLoader()
{
    m_pImage = nullptr;
    delete[] m_pData;
}


void PalmBinaryLoader::initialize(IBinaryImage *image, IBinarySymbolTable *symbols)
{
    m_image   = image;
    m_symbols = symbols;
}


namespace
{
struct SectionParams
{
    QString name;
    Address from, to;
    HostAddress hostAddr;
};
}

bool PalmBinaryLoader::loadFromMemory(QByteArray& img)
{
    long size = img.size();

    m_pImage = (uint8_t *)img.data();

    // Check type at offset 0x3C; should be "appl" (or "palm"; ugh!)
    if ((strncmp((char *)(m_pImage + 0x3C), "appl", 4) != 0) && (strncmp((char *)(m_pImage + 0x3C), "panl", 4) != 0) &&
        (strncmp((char *)(m_pImage + 0x3C), "libr", 4) != 0)) {
        qWarning() << "This is not a standard .prc file";
        return false;
    }

    addTrapSymbols();
    // Get the number of resource headers (one section per resource)

    uint32_t numSections = (m_pImage[0x4C] << 8) + m_pImage[0x4D];

    // Iterate through the resource headers (generating section info structs)
    unsigned char              *p  = m_pImage + 0x4E; // First resource header
    unsigned                   off = 0;
    std::vector<SectionParams> params;

    for (unsigned i = 0; i < numSections; i++) {
        // Now get the identifier (2 byte binary)
        unsigned   id = (p[4] << 8) + p[5];
        QByteArray qba((char *)p, 4);
        // First the name (4 alphanumeric characters from p to p+3)
        // Join the id to the name, e.g. code0, data12
        QString name = QString("%1%2").arg(QString(qba)).arg(id);

        p  += 4 + 2;
        off = UINT4(p);
        p  += 4;

        Address start_addr(off);
        // Guess the length
        if (i > 0) {
            params.back().to = start_addr;
        }

        params.push_back({ name, start_addr, Address::INVALID, HostAddress(m_pImage + off) }); // Address::INVALID will be overwritten
    }

    // Set the length for the last section
    params.back().to = params.back().from + size - off;

    for (SectionParams param : params) {
        assert(param.to != Address::INVALID);
        IBinarySection *sect = m_image->createSection(param.name, param.from, param.to);

        if (sect) {
            // Decide if code or data; note that code0 is a special case (not code)
            sect->setHostAddr(param.hostAddr)
               .setCode((param.name != "code0") && (param.name.startsWith("code")))
               .setData(param.name.startsWith("data"))
               .setEndian(0)                          // little endian
               .setEntrySize(1)                       // No info available
               .addDefinedArea(param.from, param.to); // no BSS
        }
    }

    // Create a separate, uncompressed, initialised data section
    IBinarySection *pData = m_image->getSectionInfoByName("data0");

    if (pData == nullptr) {
        fprintf(stderr, "No data section!\n");
        return false;
    }

    IBinarySection *pCode0 = m_image->getSectionInfoByName("code0");

    if (pCode0 == nullptr) {
        fprintf(stderr, "No code 0 section!\n");
        return false;
    }

    // When the info is all boiled down, the two things we need from the
    // code 0 section are at offset 0, the size of data above a5, and at
    // offset 4, the size below. Save the size below as a member variable
    m_sizeBelowA5 = UINT4ADDR(pCode0->getHostAddr() + 4);
    // Total size is this plus the amount above (>=) a5
    unsigned sizeData = m_sizeBelowA5 + UINT4ADDR(pCode0->getHostAddr());

    // Allocate a new data section
    m_pData = new unsigned char[sizeData];

    if (m_pData == nullptr) {
        fprintf(stderr, "Could not allocate %u bytes for data section\n", sizeData);
    }

    // Uncompress the data. Skip first long (offset of CODE1 "xrefs")
    p = (unsigned char *)(pData->getHostAddr() + 4).value();
    int start = (int)UINT4(p);
    p += 4;
    unsigned char *q   = (m_pData + m_sizeBelowA5 + start);
    bool          done = false;

    while (!done && (p < (unsigned char *)(pData->getHostAddr() + pData->getSize()).value())) {
        unsigned char rle = *p++;

        if (rle == 0) {
            done = true;
            break;
        }
        else if (rle == 1) {
            // 0x01 b_0 b_1
            // => 0x00 0x00 0x00 0x00 0xFF 0xFF b_0 b_1
            *q++ = 0;
            *q++ = 0;
            *q++ = 0;
            *q++ = 0;
            *q++ = 0xFF;
            *q++ = 0xFF;
            *q++ = *p++;
            *q++ = *p++;
        }
        else if (rle == 2) {
            // 0x02 b_0 b_1 b_2
            // => 0x00 0x00 0x00 0x00 0xFF b_0 b_1 b_2
            *q++ = 0;
            *q++ = 0;
            *q++ = 0;
            *q++ = 0;
            *q++ = 0xFF;
            *q++ = *p++;
            *q++ = *p++;
            *q++ = *p++;
        }
        else if (rle == 3) {
            // 0x03 b_0 b_1 b_2
            // => 0xA9 0xF0 0x00 0x00 b_0 b_1 0x00 b_2
            *q++ = 0xA9;
            *q++ = 0xF0;
            *q++ = 0;
            *q++ = 0;
            *q++ = *p++;
            *q++ = *p++;
            *q++ = 0;
            *q++ = *p++;
        }
        else if (rle == 4) {
            // 0x04 b_0 b_1 b_2 b_3
            // => 0xA9 axF0 0x00 b_0 b_1 b_3 0x00 b_3
            *q++ = 0xA9;
            *q++ = 0xF0;
            *q++ = 0;
            *q++ = *p++;
            *q++ = *p++;
            *q++ = *p++;
            *q++ = 0;
            *q++ = *p++;
        }
        else if (rle < 0x10) {
            // 5-0xF are invalid.
            assert(0);
        }
        else if (rle >= 0x80) {
            // n+1 bytes of literal data
            for (int k = 0; k <= (rle - 0x80); k++) {
                *q++ = *p++;
            }
        }
        else if (rle >= 40) {
            // n+1 repetitions of 0
            for (int k = 0; k <= (rle - 0x40); k++) {
                *q++ = 0;
            }
        }
        else if (rle >= 20) {
            // n+2 repetitions of b
            unsigned char b = *p++;

            for (int k = 0; k < (rle - 0x20 + 2); k++) {
                *q++ = b;
            }
        }
        else {
            // 0x10: n+1 repetitions of 0xFF
            for (int k = 0; k <= (rle - 0x10); k++) {
                *q++ = 0xFF;
            }
        }
    }

    if (!done) {
        fprintf(stderr, "Warning! Compressed data section premature end\n");
    }

    // printf("Used %u bytes of %u in decompressing data section\n",
    // p-(unsigned char*)pData->getHostAddr(), pData->size());

    // Replace the data pointer and size with the uncompressed versions

    pData->setHostAddr(HostAddress(m_pData));
    pData->resize(sizeData);
    // May as well make the native address zero; certainly the offset in the
    // file is no longer appropriate (and is confusing)
    // pData->getSourceAddr() = 0;
    m_symbols->create(getMainEntryPoint(), "PilotMain").setAttr("EntryPoint", true);
    return true;
}


#define TESTMAGIC4(buf, off, a, b, c, d)    (buf[off] == a && buf[off + 1] == b && buf[off + 2] == c && buf[off + 3] == d)
int PalmBinaryLoader::canLoad(QIODevice& dev) const
{
    unsigned char buf[64];

    dev.read((char *)buf, sizeof(buf));

    if (TESTMAGIC4(buf, 0x3C, 'a', 'p', 'p', 'l') || TESTMAGIC4(buf, 0x3C, 'p', 'a', 'n', 'l')) {
        /* PRC Palm-pilot binary */
        return 8;
    }

    return 0;
}


void PalmBinaryLoader::unload()
{
}


Address PalmBinaryLoader::getEntryPoint()
{
    assert(0); /* FIXME: Need to be implemented */
    return Address::ZERO;
}


void PalmBinaryLoader::close()
{
    // Not implemented yet
}


bool PalmBinaryLoader::postLoad(void *)
{
    // Not needed: for archives only
    return false;
}


LoadFmt PalmBinaryLoader::getFormat() const
{
    return LoadFmt::PALM;
}


Machine PalmBinaryLoader::getMachine() const
{
    return Machine::PALM;
}


bool PalmBinaryLoader::isLibrary() const
{
    return(strncmp((char *)(m_pImage + 0x3C), "libr", 4) == 0);
}


Address PalmBinaryLoader::getImageBase()
{
    return Address::ZERO;                                      /* FIXME */
}


size_t PalmBinaryLoader::getImageSize()
{
    return 0;                                     /* FIXME */
}


void PalmBinaryLoader::addTrapSymbols()
{
    for (uint32_t loc = 0xAAAAA000; loc <= 0xAAAAAFFF; ++loc) {
        // This is the convention used to indicate an A-line system call
        unsigned offset = loc & 0xFFF;

        if (offset < numTrapStrings) {
            m_symbols->create(Address(loc), trapNames[offset]);
        }
    }
}


// Specific to BinaryFile objects that implement a "global pointer"
// Gets a pair of unsigned integers representing the address of %agp,
// and the value for GLOBALOFFSET. For Palm, the latter is the amount of
// space allocated below %a5, i.e. the difference between %a5 and %agp
// (%agp points to the bottom of the global data area).
std::pair<Address, unsigned> PalmBinaryLoader::getGlobalPointerInfo()
{
    Address              agp = Address::ZERO;
    const IBinarySection *ps = m_image->getSectionInfoByName("data0");

    if (ps) {
        agp = ps->getSourceAddr();
    }

    std::pair<Address, unsigned> ret(agp, m_sizeBelowA5);
    return ret;
}


//  //  //  //  //  //  //
//  Specific for Palm   //
//  //  //  //  //  //  //

int PalmBinaryLoader::getAppID() const
{
    // The answer is in the header. Return 0 if file not loaded
    if (m_pImage == nullptr) {
        return 0;
    }

// Beware the endianness (large)
#define OFFSET_ID    0x40
    return (m_pImage[OFFSET_ID] << 24) + (m_pImage[OFFSET_ID + 1] << 16) + (m_pImage[OFFSET_ID + 2] << 8) +
           (m_pImage[OFFSET_ID + 3]);
}


// Patterns for Code Warrior
#define WILD    0x4AFC
static SWord CWFirstJump[] =
{
    0x0,     0x1,                                 // ? All Pilot programs seem to start with this
    0x487a,  0x4,                                 // pea 4(pc)
    0x0697, WILD, WILD,                           // addil #number, (a7)
    0x4e75
};                                                // rts
static SWord CWCallMain[] =
{
    0x487a,   14,                                 // pea 14(pc)
    0x487a,    4,                                 // pea 4(pc)
    0x0697, WILD, WILD,                           // addil #number, (a7)
    0x4e75
};                                                // rts
static SWord GccCallMain[] =
{
    0x3F04,                                       // movew d4, -(a7)
    0x6100, WILD,                                 // bsr xxxx
    0x3F04,                                       // movew d4, -(a7)
    0x2F05,                                       // movel d5, -(a7)
    0x3F06,                                       // movew d6, -(a7)
    0x6100, WILD
};                                                // bsr PilotMain

/***************************************************************************/ /**
 *
 * \brief      Try to find a pattern
 * \param start - pointer to code to start searching
 * \param patt - pattern to look for
 * \param pattSize - size of the pattern (in SWords)
 * \param max - max number of SWords to search
 * \returns       0 if no match; pointer to start of match if found
 ******************************************************************************/
SWord *findPattern(SWord *start, const SWord *patt, int pattSize, int max)
{
    const SWord *last = start + max;

    for ( ; start < last; start++) {
        bool found = true;

        for (int i = 0; i < pattSize; i++) {
            SWord curr = patt[i];
            SWord val  = Util::readWord(start + i, true);

            if ((curr != WILD) && (curr != val)) {
                found = false;
                break; // Mismatch
            }
        }

        if (found) {
            // All parts of the pattern matched
            return start;
        }
    }

    // Each start position failed
    return nullptr;
}


// Find the native address for the start of the main entry function.
// For Palm binaries, this is PilotMain.
Address PalmBinaryLoader::getMainEntryPoint()
{
    IBinarySection *psect = m_image->getSectionInfoByName("code1");

    if (psect == nullptr) {
        return Address::ZERO; // Failed
    }

    // Return the start of the code1 section
    uint16_t  *startCode = (uint16_t *)psect->getHostAddr().value();
    int delta = (psect->getHostAddr() - psect->getSourceAddr()).value();

    // First try the CW first jump pattern
    SWord *res = findPattern(startCode, CWFirstJump, sizeof(CWFirstJump) / sizeof(SWord), 1);

    if (res) {
        // We have the code warrior first jump. Get the addil operand
        const int   addilOp      = Util::readDWord((startCode + 5), true);
        SWord *startupCode = (SWord *)(HostAddress(startCode) + 10 + addilOp).value();
        // Now check the next 60 SWords for the call to PilotMain
        res = findPattern(startupCode, CWCallMain, sizeof(CWCallMain) / sizeof(SWord), 60);

        if (res) {
            // Get the addil operand
            const int _addilOp = Util::readDWord((res + 5), true);

            // That operand plus the address of that operand is PilotMain
            Address offset_loc = Address((Byte*)res - (Byte*)startCode + 5);
            return offset_loc + _addilOp; // ADDRESS::host_ptr(res) + 10 + addilOp - delta;
        }
        else {
            fprintf(stderr, "Could not find call to PilotMain in CW app\n");
            return Address::ZERO;
        }
    }

    // Check for gcc call to main
    res = findPattern(startCode, GccCallMain, sizeof(GccCallMain) / sizeof(SWord), 75);

    if (res) {
        // Get the operand to the bsr
        SWord bsrOp = res[7];
        return Address((HostAddress(res) - delta).value() + 14 + bsrOp);
    }

    fprintf(stderr, "Cannot find call to PilotMain\n");
    return Address::ZERO;
}


void PalmBinaryLoader::generateBinFiles(const QString& path) const
{
    for (const IBinarySection *si : *m_image) {
        const IBinarySection& psect(*si);

        if (psect.getName().startsWith("code") || psect.getName().startsWith("data")) {
            continue;
        }

        // Save this section in a file
        // First construct the file name
        int     sect_num = psect.getName().mid(4).toInt();
        QString name     = QString("%1%2.bin").arg(psect.getName().left(4)).arg(sect_num, 4, 16, QChar('0'));
        QString fullName(path);
        fullName += name;
        // Create the file
        FILE *f = fopen(qPrintable(fullName), "w");

        if (f == nullptr) {
            fprintf(stderr, "Could not open %s for writing binary file\n", qPrintable(fullName));
            return;
        }

        fwrite((void *)psect.getHostAddr().value(), psect.getSize(), 1, f);
        fclose(f);
    }
}


DEFINE_PLUGIN(PluginType::Loader, IFileLoader, PalmBinaryLoader,
              "Palm OS binary file", "0.4.0", "Boomerang developers")