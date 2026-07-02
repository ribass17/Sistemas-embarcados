/*
 * Comandos de diagnóstico para o Zephyr Shell (console ST-LINK / lpuart1).
 *
 * Uso:
 *   uart:~$ sysinfo tasks    — lista threads (nome, prio, stack)
 *   uart:~$ sysinfo heap     — estatísticas do heap do kernel
 *   uart:~$ sysinfo rt       — tempo de CPU por thread (requer THREAD_RUNTIME_STATS)
 *
 * Não intrusivo: apenas lê dados via k_thread_foreach() e k_thread_runtime_stats_get().
 * Nenhuma thread é suspensa ou bloqueada durante a leitura.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/mem_stats.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(shell_cmds, LOG_LEVEL_INF);

/* ---------- sysinfo tasks ---------- */

static void print_thread_cb(const struct k_thread *t, void *user_data)
{
	const struct shell *sh = (const struct shell *)user_data;
	const char *name = k_thread_name_get((k_tid_t)t);
	size_t unused = 0;

	k_thread_stack_space_get(t, &unused);
	size_t used = t->stack_info.size - unused;

	shell_print(sh, "  %-20s  %4d   %4zu / %4zu",
		    (name && name[0]) ? name : "(sem nome)",
		    t->base.prio,
		    used,
		    t->stack_info.size);
}

static int cmd_tasks(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	shell_print(sh, "  NOME                  PRIO   STACK_USADO / TOTAL");
	shell_print(sh, "  -------------------------------------------------------");
	k_thread_foreach_unlocked(print_thread_cb, (void *)sh);
	return 0;
}

/* ---------- sysinfo heap ---------- */

static int cmd_heap(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);

#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
	extern struct sys_heap _system_heap;
	struct sys_memory_stats stats;

	if (sys_heap_runtime_stats_get(&_system_heap, &stats) == 0) {
		shell_print(sh, "  Heap do kernel:");
		shell_print(sh, "    livre      : %zu B", stats.free_bytes);
		shell_print(sh, "    usado      : %zu B", stats.allocated_bytes);
		shell_print(sh, "    pico uso   : %zu B", stats.max_allocated_bytes);
	} else {
		shell_print(sh, "  Estatísticas de heap indisponíveis.");
	}
#else
	shell_print(sh, "  CONFIG_SYS_HEAP_RUNTIME_STATS não habilitado.");
#endif
	return 0;
}

/* ---------- sysinfo rt (runtime stats) ---------- */

static void print_rt_cb(const struct k_thread *t, void *user_data)
{
#ifdef CONFIG_THREAD_RUNTIME_STATS
	uint64_t sys_total = (uint64_t)(uintptr_t)user_data;
	struct k_thread_runtime_stats stats;

	if (k_thread_runtime_stats_get((k_tid_t)t, &stats) != 0) {
		return;
	}

	const char *name = k_thread_name_get((k_tid_t)t);
	uint32_t pct = (sys_total > 0)
		? (uint32_t)((stats.execution_cycles * 100ULL) / sys_total)
		: 0U;

	printk("  %-20s  %12llu ciclos  %3u %%\n",
	       (name && name[0]) ? name : "(sem nome)",
	       (unsigned long long)stats.execution_cycles,
	       pct);
#else
	ARG_UNUSED(t);
	ARG_UNUSED(user_data);
#endif
}

static int cmd_rt(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);

#ifdef CONFIG_THREAD_RUNTIME_STATS
	struct k_thread_runtime_stats all;
	k_thread_runtime_stats_all_get(&all);
	uint64_t sys_total = all.execution_cycles;

	shell_print(sh, "  NOME                  CICLOS CPU          CPU %%");
	shell_print(sh, "  -------------------------------------------------------");
	k_thread_foreach_unlocked(print_rt_cb, (void *)(uintptr_t)sys_total);
	return 0;
#else
	shell_print(sh, "  CONFIG_THREAD_RUNTIME_STATS nao habilitado.");
	return 0;
#endif
}

/* ---------- Registro de subcomandos ---------- */

SHELL_STATIC_SUBCMD_SET_CREATE(sysinfo_cmds,
	SHELL_CMD(tasks, NULL, "Lista threads com uso de stack", cmd_tasks),
	SHELL_CMD(heap,  NULL, "Estatísticas do heap do kernel", cmd_heap),
	SHELL_CMD(rt,    NULL, "Tempo de CPU por thread",        cmd_rt),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sysinfo, &sysinfo_cmds,
		   "Informações do sistema (não intrusivo)", NULL);
