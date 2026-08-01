#include <sys/param.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/rwlock.h>

#include <dev/i2c/i2cvar.h>
#include <dev/i2c/ddc.h>

struct ddc_mapping {
        TAILQ_ENTRY(ddc_mapping)        dm_link;
        struct device                   *dm_dev; /* Parent GPU driver instance (referred to as the GPU) */
        i2c_tag_t                       dm_tag;  /* I2C device that initiates transfers from within the GPU (I2C master) */
        i2c_addr_t                      dm_addr; /* Target monitor slave (I2C slave) */
};

TAILQ_HEAD(, ddc_mapping) ddcs = TAILQ_HEAD_INITIALIZER(ddcs);
struct rwlock ddcs_lock = RWLOCK_INITIALIZER("ddclk");

int             ddc_register(struct device *, i2c_tag_t, i2c_addr_t);
void            ddc_unregister(struct device *);
int             ddc_probe_device(struct device *, i2c_tag_t);
unsigned char   ddc_checksum(unsigned char *, unsigned int);

/*
 * Allocate a ddc_mapping for the given GPU driver instance,
 * I2C master, slave and monitor slave address and insert it at the
 * end of the TAILQ.
 */
int
ddc_register(struct device *dev, i2c_tag_t tag, i2c_addr_t addr)
{
        struct ddc_mapping *dm;
        dm = malloc(sizeof(*dm), M_DEVBUF, M_NOWAIT);

        if (dm == NULL)
                return (ENOMEM);

        dm->dm_dev = dev;
        dm->dm_tag = tag;
        dm->dm_addr = addr;

        rw_enter_write(&ddcs_lock);
        TAILQ_INSERT_TAIL(&ddcs, dm, dm_link);
        rw_exit_write(&ddcs_lock);

        return (0);
}
/*
 * Remove and free every ddc_mapping registered for the given
 * device from the registered device list.
 */
void
ddc_unregister(struct device *dev)
{
        struct ddc_mapping *dm, *next;
        rw_enter_write(&ddcs_lock);

        TAILQ_FOREACH_SAFE(dm, &ddcs, dm_link, next) {
                if (dev == dm->dm_dev) {
                        TAILQ_REMOVE(&ddcs, dm, dm_link);
                        free(dm, M_DEVBUF, sizeof(*dm));
                }
        }

        rw_exit_write(&ddcs_lock);
}

/*
 * Search the registered list of devices for a ddc_mapping whose device
 * field matches the given device.
 * Returns the matching ddc_mapping or NULL if none is found.
 *
 * Send a DDC/CI capabilities request to the device described by the given
 * ddc_mapping, and validate the response utilizing a checksum.
 * Return zero on success, or a non-zero error code on failure.
 */
int
ddc_probe_device(struct device *dev, i2c_tag_t tag)
{
        struct ddc_mapping *dm;
        unsigned char data[3];
        int err, len;
        rw_enter_read(&ddcs_lock);

        TAILQ_FOREACH(dm, &ddcs, dm_link) {
                if (dm->dm_dev == dev && dm->dm_tag == tag)
                        break;
        }

        if (dm == NULL) {
                rw_exit_read(&ddcs_lock);
                return (ENOENT);
        }

        len = 3;
        unsigned char cmd[6] = { DDC_HOST_ADDR_ODD, DDC_PFLAG | len,
            DDC_CMD_CAPS, 0x00, 0x00 };
        cmd[5] = ddc_checksum(cmd, 5);
        iic_acquire_bus(dm->dm_tag, cold ? I2C_F_POLL : 0);

        if ((err = iic_exec(dm->dm_tag, I2C_OP_WRITE_WITH_STOP, dm->dm_addr,
            cmd, sizeof cmd, 0, 0, 0)))
                goto out;

        tsleep_nsec(NULL, PWAIT, "ddc", MSEC_TO_NSEC(60));

        if ((err = iic_exec(dm->dm_tag, I2C_OP_READ_WITH_STOP, dm->dm_addr,
            0, 0, data, sizeof data, 0)))
                goto out;

        if (data[0] != DDC_DEFAULT_DEVICE_ADDR) {
                err = EIO;
                goto out;
        }

        if (ddc_checksum(data, sizeof data) != 0) {
                err = EIO;
                goto out;
        }

        err = 0;
out:
        iic_release_bus(dm->dm_tag, cold ? I2C_F_POLL : 0);
        rw_exit_read(&ddcs_lock);
        return (err);
}

/*
 * Compute the DDC/CI checksum for the given command payload,
 * starting from a value derived from the monitor's bus address
 * Returns the checksum.
 */
unsigned char
ddc_checksum(unsigned char *cmd, unsigned int len)
{
        unsigned int i, sum;
        sum = (unsigned char)(DDC_MONITOR_ADDR << 1);

        for (i = 0; i < len; i++)
                sum ^= cmd[i];

        return ((unsigned char)sum);
}
