#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <jansson.h>
#include <string.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <libgen.h>
#include "losha.h"
#include "debug.h"

//
// hashing
//
static char *_gethash(unsigned char *hash) {
    char *buff;
    int i;
    
    // translating hexsum
    buff = (char *) calloc((SHA_DIGEST_LENGTH * 2) + 1, 1);
    
    for(i = 0; i < SHA_DIGEST_LENGTH; i++)
        sprintf(&buff[i * 2], "%02x", hash[i]);
    
    return buff;
}

static char *finalize(char *buffer, size_t chunksz) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    char *buff;
    
    // checksum
    SHA1((unsigned char *) buffer, chunksz, hash);
    buff = _gethash(hash);
    
    debug("[+] chunk checksum: %s\n", buff);
    
    return buff;
}

static const char *identifier(json_t *root) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    const char *key, *buff;
    SHA_CTX ctx;
    size_t index;
    json_t *value;
    
    SHA1_Init(&ctx);
    
    json_array_foreach(root, index, value) {
        key = json_string_value(value);
        SHA1_Update(&ctx, key, strlen(key));
    }
    
    SHA1_Final(hash, &ctx);
    buff = _gethash(hash);
    
    return buff;
}

//
// files list
//
static int __compare(const void *a, const void *b) {
    return strcmp(*((char **) a), *((char **) b));
}

static const char **order(json_t *root) {
    const char **data;
    const char *key;
    size_t index = 0;
    json_t *value;
    
    data = (const char **) malloc(sizeof(char *) * json_object_size(root));
    
    json_object_foreach(root, key, value) {
        data[index] = key;
        index += 1;
    }
    
    qsort(data, json_object_size(root), sizeof(data), __compare);
    
    return data;
}

static void listdir(const char *root, const char *name, json_t *objects) {
    struct dirent *entry;
    struct stat fst;
    char temp[2048];
    json_t *size;
    DIR *dir;

    if(!(dir = opendir(name)))
        return;

    while((entry = readdir(dir))) {
        if(entry->d_type == DT_DIR) {
            char path[1024];

            int len = snprintf(path, sizeof(path) - 1, "%s/%s", name, entry->d_name);
            path[len] = 0;
            
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            listdir(root, path, objects);

        } else {
            // skip too long names
            if(strlen(name) + strlen(entry->d_name) + 2 > sizeof(temp)) {
                fprintf(stderr, "[-] filename too long, this will cause errors.\n");
                return;
            }
            
            sprintf(temp, "%s/%s", name, entry->d_name);
            
            if(stat(temp, &fst) < 0)    
                diep("stat");
            
            size = json_integer(fst.st_size);

            if(strcmp(root, name) != 0) {
                sprintf(temp, "%s/%s", name + strlen(root) + 1, entry->d_name);
                json_object_set_new(objects, temp, size);
                
            } else json_object_set_new(objects, entry->d_name, size);
        }
    }

    closedir(dir);
}

//
// chunks tools
//
static int bufferise(char *buffer, char *file, size_t size, size_t offset) {
    FILE *fp;
    
    if(!(fp = fopen(file, "r")))
        diep("fopen");
    
    if(offset)
        fseek(fp, offset, SEEK_SET);
    
    if(fread(buffer, size, 1, fp) != 1)
        diep("fread");
    
    fclose(fp);
    
    return size;
}

static json_t *chunks(char *root, const char **files, size_t length, size_t chunksz) {
    json_t *ochunks = json_array();
    char absolute[2048];
    struct stat fst;
    char *buffer = NULL;
    char *hex;
    size_t index;
    size_t load = 0;
    size_t diff = 0;
    size_t temp = 0;
    
    if(!(buffer = malloc(chunksz)))
        diep("malloc");
    
    //
    // chunking files
    //
    for(index = 0; index < length; index++) {
        sprintf(absolute, "%s/%s", root, files[index]);
        
        if(stat(absolute, &fst) < 0)
            diep("stat");
        
        if(load + fst.st_size <= chunksz) {
            // the file fill in this chunk
            debug("[+] buffering [%lu]: %s\n", fst.st_size, absolute);
            bufferise(buffer + load, absolute, fst.st_size, 0);
            
            load += fst.st_size;

        } else {
            temp = 0;

            // the file doesn't fill the buffer, adding it partialy
            while(temp < (size_t) fst.st_size) {
                diff = chunksz - load;
                
                // if this segment is smaller than the full space free
                // limiting the offset
                if(diff > fst.st_size - temp)
                    diff = fst.st_size - temp;
                
                debug("[+] buffering (partial): [%lu -> %lu / %lu] %s\n", temp, diff + temp, fst.st_size, absolute);
                
                bufferise(buffer + load, absolute, diff, 0);
                load += diff;
                temp += diff;
            
                // chunk is full, checksum
                if(load == chunksz) {
                    hex = finalize(buffer, chunksz);
                    
                    json_array_append_new(ochunks, json_string(hex));
                    free(hex);
                    
                    load = 0;
                }
            }
        }
    }
    
    free(buffer);
    
    return ochunks;
}

//
// sharing root
//
const char *sharing(char *path) {
    const char **ordered;
    const char *json;
    const char *id = NULL;            // descriptor id
    json_t *objects = json_object();  // file
    json_t *ochunks = NULL;           // ordered chunks
    json_t *root;                     // final dumps
    size_t chunksz = 4 * 1024 * 1024; // 4 MiB
    size_t length;
    
    fprintf(stderr, "[+] sharing: %s\n", path);

    // load directory content
    listdir(path, path, objects);
    length = json_object_size(objects);
    
    if(__debug)
        json_dump_object(objects);

    // ordering the files list
    debug("[+] ordering files names\n");
    ordered = order(objects);

    /*
    for(index = 0; index < length; index++)
        debug(">> %s\n", ordered[index]);
    */

    // computing chunks hash
    ochunks = chunks(path, ordered, length, chunksz);
    
    // building the id from chunks
    id = identifier(ochunks);
    
    // building losha-descriptor
    root = json_object();
    json_object_set_new(root, "id", json_string(id));
    json_object_set_new(root, "name", json_string(basename(path)));
    json_object_set_new(root, "files", objects);
    json_object_set_new(root, "chunks", ochunks);
    
    if(!(json = json_dumps(root, JSON_INDENT(4))))
        dies("json failed");
    
    // clearing
    json_decref(root);

    free(ordered);
    free((char *) id);
    
    return json;
}
