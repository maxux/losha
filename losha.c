#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include "share-dir.h"

int __debug = 0;

//
// command line arguments
//
typedef enum action_t {
    DO_HELP,
    DO_SHARE,
    DO_DAEMON,
    DO_REHASH,
    //...
    
} action_t;

static struct option long_options[] = {
	{"share",   no_argument,       0, 's'},
    {"rehash",  no_argument,       0, 'r'},
    {"daemon",  no_argument,       0, 'd'},
    
    {"target",  required_argument, 0, 't'},
    {"output",  required_argument, 0, 'o'},
    {"input",   required_argument, 0, 'i'},
	{"help",    no_argument,       0, 'h'},
    {"debug",   no_argument,       0, 'v'},
	{0, 0, 0, 0}
};

//
// utilities
//
void diep(char *str) {
    perror(str);
    exit(EXIT_FAILURE);
}

void dies(char *str) {
    fprintf(stderr, "[-] %s\n", str);
    exit(EXIT_FAILURE);
}

void usage() {
	printf("losha - local sharing:\n\n");
	printf(" --share    create a share\n");
    printf(" --rehash   check a local directory with a json file\n");
    printf(" --daemon   start the daemon (in foreground, fuck logic)\n\n");
    
	printf(" --target   local directory\n");
	printf(" --output   output json file\n\n");
    
    printf(" --debug    enable debug message\n");
	printf(" --help     display this message\n");
}

int main(int argc, char *argv[]) {
    FILE *fp;
    int i, option_index = 0;
    char *target = NULL;
    char *output = NULL;
    char *input = NULL;
    char *temp;
    action_t action = DO_HELP;

    while(1) {
		i = getopt_long(argc, argv, "st:o:h", long_options, &option_index);
		
		if(i == -1)
			break;

		switch(i) {
			case 's':
                action = DO_SHARE;
            break;
            
            case 'd':
                action = DO_DAEMON;
            break;
            
            case 'r':
                action = DO_REHASH;
            break;
            
            case 't':
                target = optarg;
            break;
            
			case 'o':
                output = optarg;
            break;
            
            case 'i':
                input = optarg;
            break;
            
            case 'v':
                __debug = 1;
            break;
			
			case '?':
            case 'h':
				action = DO_HELP;
			break;

			default:
				abort();
		}
	}

    //
    // let's do what we want
    //
    switch(action) {
        case DO_HELP:
            usage();
            exit(EXIT_FAILURE);
        break;
        
        case DO_SHARE:
            if(!target)
                dies("missing target (local directory)");

            // create the share
            temp = sharing(target);

            // output the json
            if(output) {
                if(!(fp = fopen(output, "w")))
                    diep(output);

                fputs(temp, fp);
                fclose(fp);
                
            } else puts(temp);
            
            free(temp);
        break;
        
        case DO_REHASH:
            if(!input)
                dies("missing input (input json file)");
            
            if(!target)
                dies("missing target (local directory)");
            
            rehash(target, input);
        break;
        
        case DO_DAEMON:
            // not implemented yet
        break;
    }
    
    return 0;
}
