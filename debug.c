#include <stdio.h>
#include <jansson.h>
#include <string.h>
#include "losha.h"

//
// json dumps
//
void json_dump_array(json_t *root) {
    size_t index;
    json_t *value;
    
    json_array_foreach(root, index, value) {
        printf("%s\n", json_string_value(value));
    }
}

void json_dump_object(json_t *root) {
    char *data = NULL;
    
    if(!(data = json_dumps(root, JSON_INDENT(4)))) {
        fprintf(stderr, "[-] cannot dump json\n");
        return;
    }
    
    puts(data);
    free(data);
}
