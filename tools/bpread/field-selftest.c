/* 用受控数值证明字段探针有效；不涉及 Office 或任何凭据。 */
#include <stdio.h>

extern void field_test(unsigned long long *values);
__asm__(".text\n"
        ".globl field_test\n"
        "field_test:\n"
        "push %r14\n"
        "mov %rcx,%r14\n"
        ".globl field_test_point\n"
        "field_test_point:\n"
        "nop\n"
        "pop %r14\n"
        "ret\n");

int main(void)
{
    unsigned long long values[3] = {17, 29, 41};
    field_test(values);
    puts("field-selftest-resumed");
    return 0;
}
