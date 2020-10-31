#pragma once

#ifndef CethFileList_H
#define CethFileList_H

#include <string>
#include <map>
#include <cstdbool>
#include <cstdint>

#ifndef OBJDB_H
typedef uint32_t KTID_t;
#endif

bool parseCethFileList(char* filelist, std::map<KTID_t, std::string>) {
    
}

#endif // CethFileList_H