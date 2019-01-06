//
// Created by barco on 2018/3/22.
//

#include "World.h"
#include <leveldb/filter_policy.h>
#include <leveldb/cache.h>
#include <leveldb/zlib_compressor.h>
#include <qstr.h>
#include <mapkey.h>
#include <ldbchunkjni.h>
#include <BlockNames.h>
#include "leveldb/decompress_allocator.h"
#include "Chunk.h"

#ifdef LOG_SAVDB_LOADSAVE
#define LOGE_LS(x, ...) LOGE(CAT("World: ", x), ##__VA_ARGS__);
#else
#define LOGE_LS(x, ...)
#endif

#ifdef LOG_SAVDB_OPERATION
#define LOGE_OP(x, ...) LOGE(CAT("SavDb: ", x), ##__VA_ARGS__);
#else
#define LOGE_OP(x, ...)
#endif

#ifdef LOG_SAVDB_LRU
#define LOGE_LR(x, ...) LOGE(CAT("World: ", x), ##__VA_ARGS__);
#else
#define LOGE_LR(x, ...)
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
//Static stuff

static inline uint16_t *initCrc16() {
    unsigned short Crc;
    unsigned short *table = new unsigned short[256];
    for (int i = 0; i < 256; i++) {
        Crc = i;
        for (int j = 0; j < 8; j++) {
            if (Crc & 0x1)
                Crc = (Crc >> 1) ^ 0xA001;
            else
                Crc >>= 1;
        }
        table[i] = Crc;
    }
    return table;
}

uint16_t *World::crc16table = initCrc16();

uint16_t World::getCrc16(mapkey_t *key) {
    unsigned short Crc = 0x0000;
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key))];
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key) + 1)];
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key) + 2)];
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key) + 3)];
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key) + 4)];
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key) + 5)];
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key) + 6)];
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key) + 7)];
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key) + 8)];
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key) + 9)];
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key) + 10)];
    Crc = (Crc >> 8) ^ crc16table[(Crc & 0xFF) ^ *(((unsigned char *) key) + 11)];
    return Crc;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//

void NullLogger::Logv(const char *, va_list) {}

////////////////////////////////////////////////////////////////////////////////////////////////////
//

World::World(const char *path, char version) {

    readOptions.decompress_allocator = new leveldb::DecompressAllocator();

    database = nullptr;
    subver = 8;
    for (int i = 0; i < 65536; i++) {
        chunks[i] = nullptr;
    }
    lru = nullptr;
    mru = nullptr;
    num_chunks = 0;
    num_maxchunks = 128;
    this->version = version;
    this->path = new char[strlen(path) + 1];
    strcpy(this->path, path);
}

World::~World() {
    flush();
    if (database) delete database;
    delete path;
    if (layers.str != nullptr)delete layers.str;
}

////////
//Getters & Setters

byte World::getTile(int32_t x, int32_t y, int32_t z, int32_t dim) {
    mapkey_t key = LDBKEY_STRUCT(x, z, dim);
    chunk_li *item = getChunkItem(key);
    byte ret = item->val->getTile(x & 0xf, y, z & 0xf);
    makeNewest(item);
    LOGE_OP("getTile at (%d,%d,%d)", x, y, z);
    return ret;
}

void World::setTile(int32_t x, int32_t y, int32_t z, int32_t dim, byte id, byte data) {
    mapkey_t key = LDBKEY_STRUCT(x, z, dim);
    chunk_li *item = getChunkItem(key);
    item->val->setTile(x & 0xf, y, z & 0xf, id, data);
    makeNewest(item);
    item->flag = CHUNK_LI_DIRTY;
    LOGE_OP("setTile at (%d,%d,%d)", x, y, z);
}

byte World::getData(int32_t x, int32_t y, int32_t z, int32_t dim) {
    mapkey_t key = LDBKEY_STRUCT(x, z, dim);
    chunk_li *item = getChunkItem(key);
    byte ret = item->val->getData(x & 0xf, y, z & 0xf);
    makeNewest(item);
    LOGE_OP("getData at (%d,%d,%d)", x, y, z);
    return ret;
}

void World::setData(int32_t x, int32_t y, int32_t z, int32_t dim, byte data) {
    mapkey_t key = LDBKEY_STRUCT(x, z, dim);
    chunk_li *item = getChunkItem(key);
    item->val->setData(x & 0xf, y, z & 0xf, data);
    makeNewest(item);
    item->flag = CHUNK_LI_DIRTY;
    LOGE_OP("setData at (%d,%d,%d)", x, y, z);
}

bool World::readFromDb(leveldb::Slice key, std::string *val) {
    leveldb::Status status = database->Get(readOptions, key, val);
    if (status.ok())return true;
    if (status.IsNotFound())return false;
    return false;
}

bool World::writeToDb(leveldb::Slice key, leveldb::Slice value) {
    leveldb::Status status = database->Put(leveldb::WriteOptions(), key, value);
    LOGE_LS("writeToDb %d", status.code());
    return status.ok();
}

uint8_t World::getHighestSubVer() {
    return subver;
}

chunk_li *World::getChunkItem(mapkey_t key) {
    LOGE_OP("Aquiring chunk (%d,%d)", key.x_div16, key.z_div16);
    uint16_t crc = getCrc16(&key);
    for (chunk_li *i = chunks[crc]; i != nullptr; i = i->hashnext) {
        if (memcmp(&key, &i->key, sizeof(mapkey_t)) != 0)continue;
        return i;
    }
    return loadChunk(key);
}

////////
//Load & Save

chunk_li *World::loadChunk(mapkey_t key) {
    LOGE_LS("Loading chunk (%d,%d)", key.x_div16, key.z_div16);
    char key_db[9];
    *(int32_t *) key_db = key.x_div16;
    *(int32_t *) (key_db + 4) = key.z_div16;
    key_db[8] = 118;
    std::string str;
    char ver;
    leveldb::Slice skey(key_db, 9);
    status = database->Get(readOptions, skey, &str);
    if (status.ok()) {
        if (str[0] <= 4)ver = VERSION_POCKET;
        else ver = VERSION_BEDROCK;
    } else if (status.IsNotFound()) {
        //We have to generate version and other data for a new chunk;
        ver = version;
        char buf[4]{version, 0, 0, 0};
        database->Put(leveldb::WriteOptions(), skey, leveldb::Slice(buf, 1));
        key_db[8] = 54;
        buf[0] = 2;
        database->Put(leveldb::WriteOptions(), skey, leveldb::Slice(buf, 4));
    } else {
        //;
    }
    Chunk *chunk;
    switch (ver) {
        case 4:
            chunk = new PocketChunk(this, key);
            break;
        case 7:
        default:
            chunk = new BedrockChunk(this, key);
    }
    chunk_li *li = new chunk_li;
    li->key = key;
    li->val = chunk;
    if (layers.length != 0 && status.IsNotFound()) {
        chunk->generateFlatLayers(&layers);
        li->flag = CHUNK_LI_DIRTY;
    }
    int crc = getCrc16(&key);
    chunk_li *h = chunks[crc];
    if (h == nullptr)chunks[crc] = li;
    else {
        while (h->hashnext != nullptr)h = h->hashnext;
        h->hashnext = li;
    }
    //chunk_lru_li *wrapper = new chunk_lru_li{li, mru, nullptr};
    li->lruprev = mru;
    //li->sorter = wrapper;
    if (mru != nullptr)mru->lrunext = li;
    else lru = li;
    mru = li;
    num_chunks++;
    LOGE_LS("Loaded chunk. Now %d in total.", num_chunks);
    return li;
}

void World::save() {
    for (chunk_li *item = lru; item != nullptr; item = item->lrunext) {
        LOGE_LS("Saving chunk (%d,%d)", item->key.x_div16, item->key.z_div16);
        if (item->flag == CHUNK_LI_CLEAN)continue;
        item->val->save();
        item->flag = CHUNK_LI_CLEAN;
        LOGE_LS("Saved chunk.");
    }
}

////////
//Others

int World::openDb() {

    if (database)return 0;
    leveldb::Options options;
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    options.block_cache = leveldb::NewLRUCache(40 * 1024 * 1024);
    options.write_buffer_size = 4 * 1024 * 1024;
    options.info_log = new NullLogger();
    options.compressors[0] = new leveldb::ZlibCompressorRaw(-1);
    options.compressors[1] = new leveldb::ZlibCompressor();
    options.create_if_missing = true;

    status = leveldb::DB::Open(options, path, &database);

    if (!status.ok())return status.code();

    return 0;
}

void World::closeDb() {
    if (database) delete database;
}

const char *World::test() {
    //loadChunk(mapkey_t{85, 0, 0});
    for (int i = 0, x = 1360, z = 0; i < 256; i++) {
        char *name = BlockNames::names[i];
        if (name[31] != static_cast<char>(0xff))continue;
        setTile(x, 12, z, 0, i, 0);
        z++;
        if (z == 160000) {
            z = 0;
            x++;
        }
    }
    flush();
    return "000";
}

void World::setLayers(unsigned int length, unsigned char *data) {
    layers.length = length;
    layers.str = new unsigned char[length];
    memcpy(layers.str, data, length);
}

void World::releaseLeastRecentUsedChunk() {

    LOGE_LR("Releasing lru chunk.");

    //Save it.
    if (lru->flag == CHUNK_LI_DIRTY) {
        LOGE_LS("Saving lru chunk (%d,%d)", lru->key.x_div16, lru->key.z_div16);
        lru->val->save();
        LOGE_LS("Saved chunk.");
    }
#ifdef LOG_SAVDB_LOADSAVE
    else
        LOGE_LS("No need to save lru chunk (%d,%d)", lru->key.x_div16, lru->key.z_div16);
#endif

    //Delete chunk.
    delete lru->val;

    //Delete li.
    uint16_t crc = getCrc16(&lru->key);
    for (chunk_li *i = chunks[crc], *prev = nullptr; i != nullptr; i = i->hashnext) {
        if (memcmp(&i->key, &lru->key, sizeof(mapkey_t)) == 0) {
            if (prev == nullptr) {
                chunks[crc] = lru->hashnext;
            } else {
                prev->hashnext = lru->hashnext;
            }
            break;
        }
        prev = i;
    }

    //Delete lru_li.
    chunk_li *next = lru->lrunext;
    delete lru;
    lru = next;
    lru->lruprev = nullptr;

    //Done.
    num_chunks--;
    LOGE_LR("Done. Now there're %d chunks.", num_chunks);
}

void World::makeNewest(chunk_li *li) {
    if (mru == li) {
        LOGE_OP("Current chunk already newest.");
        return;
    }
    if (lru == li)lru = li->lrunext;
    if (li->lruprev != nullptr) li->lruprev->lrunext = li->lrunext;
    if (li->lrunext != nullptr) li->lrunext->lruprev = li->lruprev;
    li->lruprev = mru;
    mru->lrunext = li;
    li->lrunext = nullptr;
    mru = li;
    LOGE_OP("Chunk made newest.");
}

void World::flush() {
    LOGE_LS("Flushing cached chunks...");
    for (chunk_li *i = lru, *j; i != nullptr; i = j) {
        if (i->flag == CHUNK_LI_DIRTY) {
            LOGE_LS("Saving chunk (%d,%d).", i->key.x_div16, i->key.z_div16);
            i->val->save();
            LOGE_LS("Saved chunk.");
        }
        j = i->lrunext;
        delete i->val;
        delete i;
    }
    lru = nullptr;
    mru = nullptr;
    num_chunks = 0;
    memset(chunks, 0, sizeof(chunks));
    LOGE_LS("Flusing done.");
}

void World::setMaxChunksCount(uint16_t limit) {
    num_maxchunks = limit;
    LOGE_LR("Current limit of cached chunks count is %d.", limit);
}

void World::setHighestSubVer(uint8_t subver) {
    this->subver = subver;
}

void World::changeFlatLayers(unsigned int length, unsigned char layers[]) {
    leveldb::Iterator *iter = database->NewIterator(readOptions);
    iter->SeekToFirst();
    qustr qlayers = qustr{length, layers};
    while (iter->Valid()) {
        leveldb::Slice key = iter->key();
        if ((key.size() == 9) && key[8] == 118) {
            const char *kch = key.ToString().c_str();
            mapkey_t mkey = mapkey_t{((int32_t *) kch)[0], ((int32_t *) kch)[1], 0};
            chunk_li *li = loadChunk(mkey);
            li->val->chflat(this->layers, qlayers);
            li->flag = CHUNK_LI_DIRTY;
            //releaseLeastRecentUsedChunk();
        }
        iter->Next();
    }
}

leveldb::Iterator *World::iterator() {
    return database->NewIterator(readOptions);
}

qstr World::getRaw(qstr key) {
    std::string val;
    database->Get(readOptions, leveldb::Slice(key.str, key.length), &val);
    unsigned int vlen = val.length();
    char *buf = new char[vlen];
    memcpy(buf, val.c_str(), vlen);
    qstr a{vlen, buf};
    return a;
}

void World::putRaw(qstr key, qstr value) {
    database->Put(leveldb::WriteOptions(),
                  leveldb::Slice(key.str, key.length),
                  leveldb::Slice(value.str, value.length));
}

//
////////////////////////////////////////////////////////////////////////////////////////////////////