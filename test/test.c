#define UART ((volatile unsigned int*)0x40081000)
void main() {
    const char* s = "Hello from LPC43xx QEMU!\n";
    while (*s) UART[0] = *s++;
    while (1);
}

/*
./configure --target-list=arm-softmmu --disable-docs
make -j$(nproc)
./build/qemu-system-arm -machine help
*/