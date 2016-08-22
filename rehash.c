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

//
// chunks tools
//
static json_t *chunkof(char *filename, const char **files, json_t *objects, json_t *ochunks, size_t chunksz) {
    size_t length = json_object_size(objects);
    json_t *hashs = json_array();
    size_t index;
    size_t skipped = 0;
    json_t *key;
    int size;
    
    for(index = 0; index < length; index++) {
        key = json_object_get(objects, files[index]);
        size = json_integer_value(key);
        
        // filename match
        if(strcmp(files[index], filename) == 0) {
            size_t init = skipped / chunksz;
            size_t rest = skipped % chunksz;
            
            // adding all chunks corresponding
            do {
                key = json_array_get(ochunks, init);
                json_array_append(hashs, key);
                
                size -= (chunksz - rest);
                
                rest = 0;  // no place left on the chunk
                init += 1; // next chunk

            } while(size > 0);

        } else skipped += size;
    }
    
    return hashs;
}

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

static int sizes(char *root, const char **files, size_t length, json_t *objects, size_t chunksz) {
    char absolute[2048];
    size_t index;
    struct stat fst;
    json_t *size;
    int corrupted = 0;
    
    for(index = 0; index < length; index++) {
        sprintf(absolute, "%s/%s", root, files[index]);

        if(stat(absolute, &fst) < 0)
            diep("stat");
        
        size = json_object_get(objects, files[index]);
        
        if(fst.st_size != (size_t) json_integer_value(size)) {
            printf("[-] filesize '%s' is incorrect\n", files[index]);
            corrupted += 1;
        }
    }
    
    return 0;
}

static int chunks(char *root, const char **files, size_t length, size_t chunksz, json_t *ochunks) {
    char absolute[2048];
    struct stat fst;
    char *buffer = NULL;
    char *hex;
    size_t index;
    size_t load = 0;
    size_t diff = 0;
    size_t temp = 0;
    size_t chunkdx = 0;
    const char *key;
    json_t *item;
    int corrupted = 0;
    
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
            
                // chunk is full, compare checksum
                if(load == chunksz) {
                    hex = finalize(buffer, chunksz);
                    
                    if(!(item = json_array_get(ochunks, chunkdx)))
                        dies("json out of bounds");
                        
                    key = json_string_value(item);
                    
                    if(strcmp(hex, key)) {
                        printf("[-] chunk %lu (%s) seems currupted\n", chunkdx, key);
                        corrupted += 1;
                    }
                    
                    free(hex);
                    
                    chunkdx += 1;
                    load = 0;
                }
            }
        }
    }
    
    free(buffer);
    
    return corrupted;
}

//
// sharing root
//
int rehash(char *target, char *input) {
    const char **ordered;
    json_t *objects = json_object();  // file
    json_t *ochunks = NULL;           // ordered chunks
    json_t *temp = NULL;
    json_t *root;                     // final dumps
    json_error_t error;
    size_t chunksz = 4 * 1024 * 1024; // 4 MiB
    size_t length;
    int corrupted;
    
    fprintf(stderr, "[+] rehash: %s with %s\n", target, input);
    
    if(!(root = json_load_file(input, 0, &error)))
        dies(error.text);

    temp = json_object_get(root, "name");
    printf("[+] share name: %s\n", json_string_value(temp));
    
    temp = json_object_get(root, "id");
    printf("[+] share unique id: %s\n", json_string_value(temp));
    
    
    printf("[+] preparing files list\n");
    objects = json_object_get(root, "files");
    
    length = json_object_size(objects);
    ordered = order(objects);
    
    ochunks = json_object_get(root, "chunks");
    
    /*
    for(index = 0; index < length; index++)
        debug(">> %s\n", ordered[index]);
    */
    
    char *x = "Island/Media/Texture/Image/IslandTransition.dds";
    chunkof(x, ordered, objects, ochunks, chunksz);
    exit(0);
    
    printf("[+] checking local directory file sizes\n");
    corrupted = sizes(target, ordered, length, objects, chunksz);
    
    printf("[+] checking local directory consistancy\n");
    corrupted = chunks(target, ordered, length, chunksz, ochunks);
    
    printf("[+] %d corrupted chunks found\n", corrupted);
    
    return 0;
}
