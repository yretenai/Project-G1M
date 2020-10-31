#pragma once

#ifndef NAMEDB_H
#define NAEMDB_H

typedef std::pair<std::string, KTID_t> NDBFileExt_t;

template<bool bBigEndian>
struct NDB {
    std::map<KTID_t, NDBFileExt_t> nameMap;
	NDB(BYTE* buffer, int bufferLen) {

    }
};

#endif // NAEMDB_H