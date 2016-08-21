#include <stdio.h>
#include "share-dir.h"

int main(int argc, char *argv[]) {
    char *target = "/tmp/loshatest";
    
    sharing(target);
    
    return 0;
}
