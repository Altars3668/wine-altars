/* 受控 HRESULT 读数，不访问账户、凭据或许可接口。 */
#include <windows.h>

__declspec(dllexport) __attribute__((naked)) void result_marker(void)
{
    __asm__ volatile("ret");
}

int main(void)
{
    __asm__ volatile("mov $0xc004f014, %%eax\n\tcall result_marker" : : : "rax", "memory");
    return 0;
}
