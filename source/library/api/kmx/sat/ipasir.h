#ifndef KMX_SAT_IPASIR_H
#define KMX_SAT_IPASIR_H

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct kmx_sat_ipasir_solver kmx_sat_ipasir_solver;

    kmx_sat_ipasir_solver* ipasir_init(void);
    void ipasir_release(kmx_sat_ipasir_solver* solver);
    void ipasir_add(kmx_sat_ipasir_solver* solver, int lit);
    void ipasir_assume(kmx_sat_ipasir_solver* solver, int lit);
    int ipasir_solve(kmx_sat_ipasir_solver* solver);
    int ipasir_val(const kmx_sat_ipasir_solver* solver, int lit);
    int ipasir_failed(const kmx_sat_ipasir_solver* solver, int lit);

#ifdef __cplusplus
}
#endif

#endif
