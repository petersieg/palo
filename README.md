# palo

## Synopsis

An in-progress microcode assembler together with an archiver tool and a simulator for the Xerox Alto workstation. The assembler was reconstructed from this [memo](http://www.bitsavers.org/pdf/xerox/alto/memos_1974/Alto_Microassembler_Aug74.pdf). The simulator was based on the [ContrAlto](https://github.com/livingcomputermuseum/ContrAlto) program.

## Getting started

### Prerequisites

Palo requires GCC, MAKE and SDL2 to be installed on the system.

### Building

To build the palo tools and simulator, simply type the following commands on the root directory of the palo source code repository:

```sh
$ make
```

This will compile palo with no debugging information and with some compiler optimizations enabled. To enable debugging information, the user can specify `DEBUG=1` in the command line above, such as:

```sh
$ DEBUG=1 OPTIMIZE=0 make
```
### running

./palo <disk_image>

# par

A tool for handling alto disk images:

```Usage:
 ./par [options] disk1
where:
  -1 disk1          The first disk file
  -2 disk2          The second disk file
  -f                To format the disk
  -b name           To install the boot file
  -s                Scavenges the filesystem
  -wfp              To wipe free pages
  -d dir_name       Lists the contents of a directory
  -e name filename  Extracts a given file
  -i filename name  Inserts a given file
  -c src dst        Copies from src to dst
  -r name           Removes the link to name
  -m dir_name       Creates a new directory
  -nru              To not remove underlying files
  -nud              To not update disk descriptor
  -rw               Operate in read-write mode (default is read-only)
  -ibfs             To use the BFS format for input
  -obfs             To use the BFS format for output
  -v                Increase verbosity
  --help            Print this help
```
Ex:
```ichs-iMac:src ich$ ./par -1 $HOME/Downloads/test/allgames.dsk -i $HOME/Downloads/test/hello.bcpl hello.bcpl -rw
loading disk image `/Users/ich/Downloads/test/allgames.dsk`
filesystem checked: 35 free pages
inserted `/Users/ich/Downloads/test/hello.bcpl` as `hello.bcpl` successfully
saving disk image `/Users/ich/Downloads/test/allgames.dsk`
```

# adar

```void usage(const char *prog_name)
{
    printf("Usage:\n");
    printf(" %s [options] [dir/file] disk\n", prog_name);
    printf("where:\n");
    printf("  -l            Lists all files in the filesystem\n");
    printf("  -d dirname    Lists the contents of a directory\n");
    printf("  -e filename   Extracts a given file\n");
    printf("  -r filename   Replaces a given file\n");
    printf("  -s            Scavenges files instead of finding them\n");
    printf("  -v            Increase verbosity\n");
    printf("  --help        Print this help\n");
}
```
