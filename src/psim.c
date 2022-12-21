
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "simulator/simulator.h"
#include "simulator/disk.h"
#include "simulator/display.h"
#include "simulator/ethernet.h"
#include "microcode/microcode.h"
#include "gui/gui.h"
#include "common/utils.h"

/* Data structures and types. */

/* Structure defining a breakpoint. */
struct breakpoint {
    int available;                /* Breakpoint available. */
    int enable;                   /* Breakpoint enabled. */
    uint8_t task;                 /* The task of the breakpoint. */
    uint8_t ntask;                /* The next task of the breakpoint. */
    uint16_t mpc;                 /* The micro program counter. */
    int on_task_switch;           /* Only enable on task switch. */
    uint32_t mir_fmt;             /* The format of the micro instruction
                                   * to define a break point.
                                   */
    uint32_t mir_mask;            /* The mask for the micro instruction
                                   * breakpoint.
                                   */
};

/* Internal structure for the psim simulator. */
struct psim {
    const char *const_filename;   /* The name of the constant rom. */
    const char *mcode_filename;   /* The name of the microcode rom. */
    const char *disk1_filename;   /* Disk 1 image file. */
    const char *disk2_filename;   /* Disk 2 image file. */

    struct gui ui;                /* The user input. */
    struct simulator sim;         /* The simulator. */

    size_t max_breakpoints;       /* The maximum number of breakpoints. */
    struct breakpoint *bps;       /* The breakpoints. */

    char *cmd_buf;                /* Buffer for command. */
    size_t cmd_buf_size;          /* Size of the command buffer. */

    char *out_buf;                /* Buffer for output. */
    size_t out_buf_size;          /* Size of the output buffer. */
    struct string_buffer output;  /* The string buffer for output. */
};

/* Forward declarations. */
static int psim_debug(struct gui *ui);

/* Functions. */

/* Initializes the psim variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
static
void psim_initvar(struct psim *ps)
{
    simulator_initvar(&ps->sim);
    gui_initvar(&ps->ui);

    ps->bps = NULL;
    ps->cmd_buf = NULL;
    ps->out_buf = NULL;
}

/* Destroys the psim object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
static
void psim_destroy(struct psim *ps)
{
    if (ps->bps) free((void *) ps->bps);
    ps->bps = NULL;

    if (ps->cmd_buf) free((void *) ps->cmd_buf);
    ps->cmd_buf = NULL;

    if (ps->out_buf) free((void *) ps->out_buf);
    ps->out_buf = NULL;

    gui_destroy(&ps->ui);
    simulator_destroy(&ps->sim);
}

/* Creates a new psim object.
 * This obeys the initvar / destroy / create protocol.
 * The `sys_type` variable specifies the system type.
 * The name of the several filenames to load related to the constant rom,
 * microcode rom, and disk images are given by the parameters:
 * `const_filename`, `mcode_filename`, `disk1_filename`, and
 * `disk2_filename`, respectively.
 * Returns TRUE on success.
 */
static
int psim_create(struct psim *ps,
                enum system_type sys_type,
                const char *const_filename,
                const char *mcode_filename,
                const char *disk1_filename,
                const char *disk2_filename)
{
    psim_initvar(ps);

    ps->max_breakpoints = 1024;
    ps->cmd_buf_size = 8192;
    ps->out_buf_size = 8192;

    ps->bps = (struct breakpoint *)
        malloc(ps->max_breakpoints * sizeof(struct breakpoint));
    ps->cmd_buf = (char *) malloc(ps->cmd_buf_size);
    ps->out_buf = (char *) malloc(ps->out_buf_size);

    if (unlikely(!ps->bps || !ps->cmd_buf || !ps->out_buf)) {
        report_error("psim: create: memory exhausted");
        psim_destroy(ps);
        return FALSE;
    }

    if (unlikely(!simulator_create(&ps->sim, sys_type))) {
        report_error("psim: create: could not create simulator");
        psim_destroy(ps);
        return FALSE;
    }

    if (unlikely(!gui_create(&ps->ui, &ps->sim, &psim_debug, ps))) {
        report_error("psim: create: could not create user interface");
        return FALSE;
    }

    ps->const_filename = const_filename;
    ps->mcode_filename = mcode_filename;
    ps->disk1_filename = disk1_filename;
    ps->disk2_filename = disk2_filename;

    ps->output.buf = ps->out_buf;
    ps->output.buf_size = ps->out_buf_size;

    return TRUE;
}

/* Runs the PSIM simulator.
 * Returns TRUE on success.
 */
static
int psim_run(struct psim *ps)
{
    const char *fn;

    fn = ps->const_filename;
    if (unlikely(!simulator_load_constant_rom(&ps->sim, fn))) {
        report_error("psim: run: could not load constant rom");
        return FALSE;
    }

    fn = ps->mcode_filename;
    if (unlikely(!simulator_load_microcode_rom(&ps->sim, fn, 0))) {
        report_error("psim: run: could not load microcode rom");
        return FALSE;
    }

    fn = ps->disk1_filename;
    if (fn) {
        if (unlikely(!disk_load_image(&ps->sim.dsk, 0, fn))) {
            report_error("psim: run: could not load disk 1");
            return FALSE;
        }
    }

    fn = ps->disk2_filename;
    if (fn) {
        if (unlikely(!disk_load_image(&ps->sim.dsk, 1, fn))) {
            report_error("psim: run: could not load disk 2");
            return FALSE;
        }
    }

    simulator_reset(&ps->sim);

    if (unlikely(!gui_start(&ps->ui))) {
        report_error("psim: run: could not start user interface");
        return FALSE;
    }

    return TRUE;
}

/* Gets a command line from the standard input.
 * The command line is stored in `ps->cmd_buf`, with the words separated by a
 * NUL character. The last word is ended with two consecutive NUL characters.
 */
static
void psim_get_command(struct psim *ps)
{
    size_t i, len;
    int c, last_is_space;

    printf(">");

    i = len = 0;
    last_is_space = TRUE;
    while (TRUE) {
        c = fgetc(stdin);
        if (c == EOF) break;
        if (c == '\n') break;

        if (isspace(c)) {
            if (last_is_space) continue;
            last_is_space = TRUE;
            c = '\0';
        } else {
            last_is_space = FALSE;
        }

        if (i + 2 < ps->cmd_buf_size)
            ps->cmd_buf[i++] = (char) c;
        len++;
    }

    /* Return the same command as before. */
    if (i == 0) return;

    if (!last_is_space) {
        ps->cmd_buf[i++] = '\0';
        len++;
    }
    ps->cmd_buf[i++] = '\0';
    len++;

    if (len >= ps->cmd_buf_size) {
        printf("command too long\n");
        ps->cmd_buf[0] = '\0';
        ps->cmd_buf[1] = '\0';
    }
}

/* Runs the simulation.
 * The parameter `max_steps` specifies the maximum number of steps
 * to run. If `max_steps` is negative, it runs indefinitely.
 * Returns TRUE on success.
 */
static
int psim_simulate(struct psim *ps, int max_steps)
{
    const struct breakpoint *bp;
    unsigned int num, max_breakpoints;
    int step, hit, hit1;

    /* Get the effective number of breakpoints. */
    max_breakpoints = 0;
    for (num = 0; num < ps->max_breakpoints; num++) {
        bp = &ps->bps[num];
        if (!bp->available && bp->enable) {
            max_breakpoints = num + 1;
        }
    }

    step = 0;
    while (gui_running(&ps->ui)) {
        if (max_steps >= 0 && step == max_steps)
            break;

        simulator_step(&ps->sim);
        step++;

        if ((step % 100000) == 0) {
            if (unlikely(!gui_update(&ps->ui))) {
                report_error("psim: simulate: could not update GUI");
                return FALSE;
            }
        }

        hit = FALSE;
        for (num = 0; num < max_breakpoints; num++) {
            bp = &ps->bps[num];
            if (!bp->enable) continue;

            hit1 = TRUE;
            if (bp->task != 0xFF) {
                if (bp->task != ps->sim.ctask)
                    hit1 = FALSE;
            }

            if (bp->ntask != 0xFF) {
                if (bp->ntask != ps->sim.ntask)
                    hit1 = FALSE;
            }

            if (bp->mpc != 0xFFFF) {
                if (bp->mpc != ps->sim.mpc)
                    hit1 = FALSE;
            }

            if (bp->on_task_switch) {
                if (!ps->sim.task_switch)
                    hit1 = FALSE;
            }

            if (bp->mir_mask != 0) {
                if ((ps->sim.mir & bp->mir_mask) != bp->mir_fmt)
                    hit1= FALSE;
            }

            if (hit1) {
                if (num > 0) {
                    printf("breakpoint %u hit\n", num);
                }
                hit = TRUE;
                break;
            }
        }

        if (hit) break;
    }

    return TRUE;
}

/* Processes the registers command.
 * If the `extra` parameter is set to TRUE, it prints the extra
 * registers instead.
 */
static
void psim_cmd_registers(struct psim *ps, int extra)
{
    string_buffer_reset(&ps->output);
    simulator_disassemble(&ps->sim, &ps->output);
    printf("%s\n", ps->out_buf);

    string_buffer_reset(&ps->output);
    if (extra) {
        simulator_print_extra_registers(&ps->sim, &ps->output);
    } else {
        simulator_print_registers(&ps->sim, &ps->output);
    }
    printf("%s\n", ps->out_buf);
}

/* Shows the disk registers. */
static
void psim_cmd_disk_registers(struct psim *ps)
{
    string_buffer_reset(&ps->output);
    disk_print_registers(&ps->sim.dsk, &ps->output);
    printf("%s\n", ps->out_buf);
}

/* Shows the display registers. */
static
void psim_cmd_display_registers(struct psim *ps)
{
    string_buffer_reset(&ps->output);
    display_print_registers(&ps->sim.displ, &ps->output);
    printf("%s\n", ps->out_buf);
}

/* Shows the ethernet registers. */
static
void psim_cmd_ethernet_registers(struct psim *ps)
{
    string_buffer_reset(&ps->output);
    ethernet_print_registers(&ps->sim.ether, &ps->output);
    printf("%s\n", ps->out_buf);
}

/* Dumps the contents of memory. */
static
void psim_cmd_dump_memory(struct psim *ps)
{
    const char *arg, *end;
    unsigned int num;
    uint16_t addr, val;

    arg = (const char *) ps->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    num = 8;
    if (arg[0] != '\0') {
        addr = (uint16_t) strtoul(arg, (char **) &end, 8);
        if (end[0] != '\0') {
            printf("invalid address (octal number) %s\n", arg);
            return;
        }
        arg = &arg[strlen(arg) + 1];

        if (arg[0] != '\0') {
            num = strtoul(arg, (char **) &end, 10);
            if (end[0] != '\0') {
                printf("invalid number %s\n", arg);
            }
        }
    } else {
        addr = 0;
    }

    while ((num-- > 0) && gui_running(&ps->ui)) {
        val = simulator_read(&ps->sim, addr, ps->sim.ctask, FALSE);
        printf("%06o: %06o\n", addr++, val);
    }
}

/* Processes the continue command.
 * Returns TRUE on success.
 */
static
int psim_cmd_continue(struct psim *ps)
{
    ps->bps[0].enable = FALSE;
    if (unlikely(!psim_simulate(ps, -1))) {
        report_error("psim: cmd_continue: could not simulate");
        return FALSE;
    }

    psim_cmd_registers(ps, FALSE);
    return TRUE;
}

/* Processes the "next" command.
 * Returns TRUE on success.
 */
static
int psim_cmd_next(struct psim *ps)
{
    const char *arg, *end;
    int num;

    arg = (const char *) ps->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] != '\0') {
        num = (int) strtoul(arg, (char **) &end, 10);
        if (end[0] != '\0' || num < 0) {
            printf("invalid number %s\n", arg);
            return TRUE;
        }
    } else {
        num = 1;
    }

    ps->bps[0].enable = FALSE;
    if (unlikely(!psim_simulate(ps, num))) {
        report_error("psim: cmd_next: could not simulate");
        return FALSE;
    }

    psim_cmd_registers(ps, FALSE);
    return TRUE;
}

/* Processes the "next task" command.
 * Returns TRUE on success.
 */
int psim_cmd_next_task(struct psim *ps)
{
    struct breakpoint *bp;
    const char *arg, *end;
    uint8_t task;

    arg = (const char *) ps->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] != '\0') {
        task = (uint8_t) strtoul(arg, (char **) &end, 8);
        if (end[0] != '\0') {
            printf("invalid task (octal number) %s\n", arg);
            return TRUE;
        }
    } else {
        task = 0xFF;
    }

    bp = &ps->bps[0];
    bp->enable = TRUE;
    bp->task = task;
    bp->ntask = 0xFF;
    bp->mpc = 0xFFFF;
    bp->on_task_switch = TRUE;
    bp->mir_mask = 0;
    bp->mir_fmt = 0;

    if (unlikely(!psim_simulate(ps, -1))) {
        report_error("psim: cmd_next_task: could not simulate");
        return FALSE;
    }

    psim_cmd_registers(ps, FALSE);
    return TRUE;
}

/* Adds a breakpoint based on the string in the command buffer. */
static
void psim_cmd_add_breakpoint(struct psim *ps)
{
    const char *arg, *end;
    struct breakpoint *bp;
    unsigned int num;

    for (num = 1; num < ps->max_breakpoints; num++) {
        if (ps->bps[num].available) break;
    }
    if (num >= ps->max_breakpoints) {
        printf("maximum number of breakpoints reached\n");
        return;
    }

    bp = &ps->bps[num];
    bp->enable = FALSE;
    bp->task = 0xFF;
    bp->ntask = 0xFF;
    bp->mpc = 0xFFFF;
    bp->on_task_switch = FALSE;
    bp->mir_fmt = 0;
    bp->mir_mask = 0;

    arg = (const char *) ps->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    while (arg[0] != '\0') {
        if (strcmp(arg, "-task") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the task\n");
                return;
            }
            bp->task = (uint8_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid task (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-ntask") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the ntask\n");
                return;
            }
            bp->ntask = (uint8_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid ntask (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-on_task_switch") == 0) {
            arg = &arg[strlen(arg) + 1];
            bp->on_task_switch = TRUE;
            bp->enable = TRUE;
            continue;
        }

        if (strcmp(arg, "-mir") == 0) {
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the MIR format\n");
                return;
            }
            bp->mir_fmt = (uint32_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid MIR format (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];
            if (arg[0] == '\0') {
                printf("please specify the MIR mask\n");
                return;
            }
            bp->mir_mask = (uint32_t) strtoul(arg, (char **) &end, 8);
            if (end[0] != '\0') {
                printf("invalid MIR mask (octal number) %s\n", arg);
                return;
            }
            arg = &arg[strlen(arg) + 1];
            bp->enable = TRUE;
            continue;
        }

        bp->mpc = (uint16_t) strtoul(arg, (char **) &end, 8);
        if (end[0] != '\0') {
            printf("invalid MPC (octal number) %s\n", arg);
            return;
        }
        arg = &arg[strlen(arg) + 1];
        bp->enable = TRUE;
    }

    if (!bp->enable) {
        printf("no breakpoint defined\n");
        return;
    }

    bp->available = FALSE;
    printf("breakpoint %u created\n", num);
}

/* Enables or disables a breakpoint based on the parameter `enable`. */
static
void psim_cmd_breakpoint_enable(struct psim *ps, int enable)
{
    const char *arg, *end;
    unsigned int num;

    arg = (const char *) ps->cmd_buf;
    arg = &arg[strlen(arg) + 1];

    if (arg[0] == '\0') {
        printf("please specify a breakpoint number\n");
        return;
    }

    num = strtoul(arg, (char **) &end, 10);
    if (end[0] != '\0' || num == 0) {
        printf("invalid breakpoint number %s\n", arg);
        return;
    }

    if (num >= ps->max_breakpoints) {
        printf("breakpoint number exceeds maximum available\n");
        return;
    }

    ps->bps[num].enable = enable;
    printf("breakpoint %d %s\n",
           num, (enable) ? "enabled" : "disabled");
}

/* To run the debugger.
 * The parameter `ui` contains a reference to the gui object, which
 * in turn contains a reference to the psim object via `ui->arg`.
 * Returns TRUE on success.
 */
static
int psim_debug(struct gui *ui)
{
    struct psim *ps;
    unsigned int num;
    const char *cmd;

    ps = (struct psim *) ui->arg;

    for (num = 1; num < ps->max_breakpoints; num++) {
        ps->bps[num].available = TRUE;
    }
    ps->bps[0].available = FALSE;

    ps->cmd_buf[0] = '\0';
    ps->cmd_buf[1] = '\0';

    while (gui_running(ui)) {
        if (unlikely(!gui_update(ui))) {
            report_error("psim: debug: could not update GUI");
            return FALSE;
        }

        psim_get_command(ps);

        cmd = (const char *) ps->cmd_buf;

        if (strcmp(cmd, "r") == 0) {
            psim_cmd_registers(ps, FALSE);
            continue;
        }

        if (strcmp(cmd, "e") == 0) {
            psim_cmd_registers(ps, TRUE);
            continue;
        }

        if (strcmp(cmd, "dsk") == 0) {
            psim_cmd_disk_registers(ps);
            continue;
        }

        if (strcmp(cmd, "displ") == 0) {
            psim_cmd_display_registers(ps);
            continue;
        }

        if (strcmp(cmd, "ether") == 0) {
            psim_cmd_ethernet_registers(ps);
            continue;
        }

        if (strcmp(cmd, "d") == 0) {
            psim_cmd_dump_memory(ps);
            continue;
        }

        if (strcmp(cmd, "c") == 0) {
            if (unlikely(!psim_cmd_continue(ps)))
                return FALSE;
            continue;
        }

        if (strcmp(cmd, "n") == 0) {
            if (unlikely(!psim_cmd_next(ps)))
                return FALSE;
            continue;
        }

        if (strcmp(cmd, "nt") == 0) {
            if (unlikely(!psim_cmd_next_task(ps)))
                return FALSE;
            continue;
        }

        if (strcmp(cmd, "bp") == 0) {
            psim_cmd_add_breakpoint(ps);
            continue;
        }

        if (strcmp(cmd, "be") == 0) {
            psim_cmd_breakpoint_enable(ps, TRUE);
            continue;
        }

        if (strcmp(cmd, "bd") == 0) {
            psim_cmd_breakpoint_enable(ps, FALSE);
            continue;
        }

        if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
            printf("Commands:\n");
            printf("  r           Print the registers\n");
            printf("  e           Print the extra registers\n");
            printf("  dsk         Print the disk registers\n");
            printf("  displ       Print the display registers\n");
            printf("  ether       Print the ethernet registers\n");
            printf("  d [addr]    Dump the memory contents\n");
            printf("  c           Continue execution\n");
            printf("  n [num]     Step through the microcode\n");
            printf("  nt [task]   Step until switch task\n");
            printf("  bp specs    Adds a breakpoint\n");
            printf("  be num      Enables a breakpoint\n");
            printf("  bd num      Disables a breakpoint\n");
            printf("  h           Print this help\n");
            printf("  q           Quit the debugger\n");
            continue;
        }

        if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
            gui_stop(ui);
            break;
        }

        printf("invalid command\n");
        ps->cmd_buf[0] = '\0';
        ps->cmd_buf[1] = '\0';
    }

    return TRUE;
}

/* Print the program usage information. */
static
void usage(const char *prog_name)
{
    printf("Usage:\n");
    printf(" %s [options] microcode\n", prog_name);
    printf("where:\n");
    printf("  -c constant   Specify the constant rom file\n");
    printf("  -m micro      Specify the microcode rom file\n");
    printf("  -1 disk1      Specify the disk 1 filename\n");
    printf("  -2 disk2      Specify the disk 2 filename\n");
    printf("  --help        Print this help\n");
}

int main(int argc, char **argv)
{
    const char *const_filename;
    const char *mcode_filename;
    const char *disk1_filename;
    const char *disk2_filename;
    struct psim ps;
    int i, is_last;

    psim_initvar(&ps);
    const_filename = NULL;
    mcode_filename = NULL;
    disk1_filename = NULL;
    disk2_filename = NULL;

    for (i = 1; i < argc; i++) {
        is_last = (i + 1 == argc);
        if (strcmp("-c", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the constant rom file");
                return 1;
            }
            const_filename = argv[++i];
        } else if (strcmp("-m", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the microcode rom file");
                return 1;
            }
            mcode_filename = argv[++i];
        } else if (strcmp("-1", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the disk 1 file");
                return 1;
            }
            disk1_filename = argv[++i];
        } else if (strcmp("-2", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the disk 2 file");
                return 1;
            }
            disk2_filename = argv[++i];
        } else if (strcmp("--help", argv[i]) == 0
                   || strcmp("-h", argv[i]) == 0) {
            usage(argv[0]);
            return 0;
        } else {
            disk1_filename = argv[i];
        }
    }

    if (!mcode_filename) {
        report_error("main: must specify the microcode rom file name");
        return 1;
    }

    if (!const_filename) {
        report_error("main: must specify the constant rom file name");
        return 1;
    }

    if (unlikely(!psim_create(&ps, ALTO_II_3KRAM,
                              const_filename, mcode_filename,
                              disk1_filename, disk2_filename))) {
        report_error("main: could not create psim object");
        return 1;
    }

    if (unlikely(!psim_run(&ps))) {
        report_error("main: error while running");
        psim_destroy(&ps);
        return 1;
    }

    psim_destroy(&ps);
    return 0;
}
