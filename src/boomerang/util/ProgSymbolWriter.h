#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#pragma once


class Prog;
class QString;


/**
 * Prints symbols of a Prog to a file.
 */
class ProgSymbolWriter
{
public:
    bool writeSymbolsToFile(const Prog *prog, const QString &dstFileName);
};
