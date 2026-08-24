#include <assert.h>

#include <proc_tree.h>
#include <astra/proc.h>

int
main(void)
{
    assert(supervisor_proc_path_is_root(NULL));
    assert(supervisor_proc_path_is_root(""));
    assert(supervisor_proc_path_is_root("/"));
    assert(!supervisor_proc_path_is_root("//"));
    assert(!supervisor_proc_path_is_root("/1"));
    assert(supervisor_proc_path_is_snapshot("snapshot"));
    assert(supervisor_proc_path_is_snapshot("/snapshot"));
    assert(!supervisor_proc_path_is_snapshot(NULL));
    assert(!supervisor_proc_path_is_snapshot("snapshot/"));
    assert(!supervisor_proc_path_is_snapshot("//snapshot"));
    assert(sizeof(AstraProcSnapshot) == 112u);
    assert(ASTRA_PROC_NAME_MAX == 32u);
    return 0;
}
