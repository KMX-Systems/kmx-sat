#include <kmx/sat/ipasir.h>

int main(void)
{
    kmx_sat_ipasir_solver* solver = ipasir_init();
    if (solver == 0)
        return 1;
    ipasir_add(solver, 1);
    ipasir_add(solver, 0);
    if (ipasir_solve(solver) != 10 || ipasir_val(solver, 1) != 1)
    {
        ipasir_release(solver);
        return 2;
    }
    ipasir_release(solver);
    return 0;
}
