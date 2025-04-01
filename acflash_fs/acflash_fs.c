#include "irx_imports.h"
#include <errno.h>
#include "../../acflash_common.h"
#include <stdint.h>

#define FUNLOG() M_PRINTF("%s\n", __func__)
#define M_PRINTF(format, args...) printf(MODNAME ": " format, ##args) // module printf

#define MODNAME "acflash_fs"
IRX_ID(MODNAME, 1, 1);

acFlashInfoData acflash_info;
#define ACFLASH_SIZEOF() (acflash_info.fi_bsize * acflash_info.fi_blocks)

int mmce_fs_unregister(void);
int acflash_fs_register(void);

int _start(int argc, char** argv) {
    if (argc < 0) {//IRX UNLOAD
        mmce_fs_unregister();
        return MODULE_NO_RESIDENT_END;
    }
    int res = 0;
    res = acFlashInfo(&acflash_info);
    if (res == 0) M_PRINTF("flash bsize:%d bcount:%d (%d)\n", acflash_info.fi_bsize, acflash_info.fi_blocks, ACFLASH_SIZEOF());
    else M_PRINTF("Error getting FLASH info %d\n", res);
    acflash_fs_register();
    return res;
}

int acflash_open, acflash_seekpos;


int acflash_fs_init(iomanX_iop_device_t *f)
{
    acflash_open = 0;
    acflash_seekpos = 0;
    FUNLOG();
    return 0;
}

int acflash_fs_deinit(iomanX_iop_device_t *f)
{
    FUNLOG();
    return 0;
}


int acflash_fs_read(iomanX_iop_file_t *file, void *ptr, int size)
{
    FUNLOG();
    int bytes_read;
    bytes_read = acFlashRead(acflash_seekpos, ptr, size);
    M_PRINTF("Readed %d bytes from flash\n", bytes_read);
    return bytes_read;
}


int acflash_fs_write(iomanX_iop_file_t *file, void *ptr, int size)
{
    FUNLOG();
    int bytes_written;
    bytes_written = acFlashProgram(acflash_seekpos, ptr, size);
    M_PRINTF("Written %d bytes from flash\n", bytes_written);
    return bytes_written;
}

int acflash_fs_devctl(iomanX_iop_file_t *fd, const char *name, int cmd, void *arg, unsigned int arglen, void *buf, unsigned int buflen)
{
    FUNLOG();
    int res = 0;
    switch (cmd)
    {
    case ACFLASH_FS_GET_BLOCKSIZE:
        res = acflash_info.fi_bsize;
        break;
    case ACFLASH_FS_GET_BLOCKCONT:
        res = acflash_info.fi_blocks;
        break;
    case ACFLASH_FS_GET_SIZE_BYTE:
        res = ACFLASH_SIZEOF();
        break;
    case ACFLASH_FS_REFRESH_FINFO:
        res = acFlashInfo(&acflash_info);
        break;
    
    default:
        break;
    }
    return res;
}

//ACFLASH has no FS that we care of. open/close will just reference opening/closing the actual ACFLASH, not files inside
int acflash_fs_open(iomanX_iop_file_t *file)
{
    if (acflash_open) return -EINVAL;
    acflash_open = 1;
    acflash_seekpos = 0;
    FUNLOG();
    return 0;
}

int acflash_fs_close(iomanX_iop_file_t *file)
{
    acflash_open = 0;
    acflash_seekpos = 0;
    FUNLOG();
    return 0;
}
int acflash_fs_lseek(iomanX_iop_file_t *file, int offset, int whence)
{
    int startPos, newPos;
    FUNLOG();
    switch (whence)
    {
        case FIO_SEEK_SET:
            startPos = 0;
            break;
        case FIO_SEEK_CUR:
            startPos = acflash_seekpos;
            break;
        case FIO_SEEK_END:
            startPos = ACFLASH_SIZEOF();
            break;
        default:
            return -EINVAL;
    }
    newPos = startPos + offset;
    if (newPos > ACFLASH_SIZEOF()) return -EINVAL; //don't go beyond flash end
    acflash_seekpos = newPos;
    return acflash_seekpos;
}

#define NOT_SUPPORTED_OP (void*)&not_supported_operation
static int not_supported_operation() {
    return -ENOTSUP;
}

static iomanX_iop_device_ops_t acflash_fio_ops =
{
	&acflash_fs_init,    //init
	&acflash_fs_deinit,  //deinit
	NOT_SUPPORTED_OP, //format
	(void*)&acflash_fs_open,    //open
	(void*)&acflash_fs_close,   //close
	&acflash_fs_read, //read
	&acflash_fs_write, //write
	&acflash_fs_lseek, //lseek
	NOT_SUPPORTED_OP, //ioctl
	NOT_SUPPORTED_OP, //remove
	NOT_SUPPORTED_OP, //mkdir
	NOT_SUPPORTED_OP, //rmdir
	NOT_SUPPORTED_OP, //dopen
	NOT_SUPPORTED_OP, //dclose
	NOT_SUPPORTED_OP, //dread
	NOT_SUPPORTED_OP, //getstat
	NOT_SUPPORTED_OP, //chstat
    //EXTENDED OPS
    NOT_SUPPORTED_OP, //rename
    NOT_SUPPORTED_OP, //chdir
    NOT_SUPPORTED_OP, //sync
    NOT_SUPPORTED_OP, //mount
    NOT_SUPPORTED_OP, //umount
    NOT_SUPPORTED_OP, //lseek64
    &acflash_fs_devctl, //devctl
    NOT_SUPPORTED_OP, //symlink
    NOT_SUPPORTED_OP, //readlink
    NOT_SUPPORTED_OP,  //ioctl2
};

static iomanX_iop_device_t acflash_dev =
{
	"acflash",
	(IOP_DT_FS | IOP_DT_FSEXT),
	1,
	"RAW access for Namco system ACFLASH",
	&acflash_fio_ops, //fill this with an instance of iomanX_iop_device_ops
};

int mmce_fs_unregister(void) {
    iomanX_DelDrv(acflash_dev.name);
    return 0;
}

int acflash_fs_register(void) {
    M_PRINTF("Registering %s device\n", acflash_dev.name);
    mmce_fs_unregister();

    if (iomanX_AddDrv(&acflash_dev)!= 0)
        return 0;

    return 1;
}
