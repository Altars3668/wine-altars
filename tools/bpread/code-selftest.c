/* 受控 CFG 目标读数：不读取账户、凭据或文件，不联网。 */
#include <windows.h>

__declspec(dllexport) int code_target(void)
{
    return 29;
}

__declspec(dllexport) __attribute__((naked)) void code_marker(void)
{
    __asm__ volatile("ret");
}

int main(int argc, char **argv)
{
    /* negative 模式使用非可执行数据地址，验证诊断不会输出该地址。 */
    register void *target __asm__("rax") = argc > 1 ? (void *)argv : (void *)code_target;
    __asm__ volatile("call code_marker" : "+a"(target) : : "memory");
    return 0;
}
