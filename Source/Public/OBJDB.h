#pragma once

#ifndef OBJDB_H
#define OBJDB_H

typedef enum class OBJDBTypeId : uint32_t {

} OBJDBTypeId_t;

typedef std::pair<OBJDBTypeId_t, uintptr_t> OBJDBTypeHint_t;

template<bool bBigEndian>
class OBJDB {
    std::map<KTID_t, std::map<KTID_t, OBJDBTypeHint_t>> objects;
    std::unique_ptr<uint8_t[]> buffer;
    size_t bufferLength;

public:
    OBJDB(uint8_t* buffer, size_t bufferLen) : buffer(new uint8_t[bufferLen]) {
        std::copy(buffer.get(), buffer.get() + bufferLength, buffer);
        bufferLength = bufferLen;
    }

    bool hasObject(KTID_t id) {

    }

    bool hasProperty(KTID_t id, KTID_t propertyId) {

    }

    template<typename T>
    T readProperty(KTID_t id, KTID_t propertyId) {

    }
};

#endif // OBJDB_H