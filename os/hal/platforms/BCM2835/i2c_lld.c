/*
    ChibiOS/RT - Copyright (C) 2006,2007,2008,2009,2010,
                 2011,2012 Giovanni Di Sirio.

    This file is part of ChibiOS/RT.

    ChibiOS/RT is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    ChibiOS/RT is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file    BCM2835/i2c_lld.c
 * @brief   I2C Driver subsystem low level driver source template.
 *
 * @addtogroup I2C
 * @{
 */

#include "ch.h"
#include "hal.h"

#if HAL_USE_I2C || defined(__DOXYGEN__)

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

I2CDriver I2C0;

/*===========================================================================*/
/* Driver local variables.                                                   */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

static void i2c_init(I2CDriver *i2c_driver, bscdevice_t *device_address) {
  i2c_driver->callback = NULL;
  i2c_driver->device = device_address;
  i2cObjectInit(&I2C0);
}

/**
 * @brief   Wakes up the waiting thread.
 *
 * @param[in] i2cp      pointer to the @p I2CDriver object
 * @param[in] msg       wakeup message
 *
 * @notapi
 */
#define wakeup_isr(i2cp, msg) {                                             \
  chSysLockFromIsr();                                                       \
  if ((i2cp)->thread != NULL) {                                             \
    Thread *tp = (i2cp)->thread;                                            \
    (i2cp)->thread = NULL;                                                  \
    tp->p_u.rdymsg = (msg);                                                 \
    chSchReadyI(tp);                                                        \
  }                                                                         \
  chSysUnlockFromIsr();                                                     \
}

/**
 * @brief   Handling of stalled I2C transactions.
 *
 * @param[in] i2cp      pointer to the @p I2CDriver object
 *
 * @notapi
 */
static void i2c_lld_safety_timeout(void *p) {
  I2CDriver *i2cp = (I2CDriver *)p;
  chSysLockFromIsr();
  if (i2cp->thread) {
    bscdevice_t *device = i2cp->device;
    device->control = 0;
    device->status = BSC_CLKT | BSC_ERR | BSC_DONE;

    Thread *tp = i2cp->thread;
    i2cp->thread = NULL;
    tp->p_u.rdymsg = RDY_TIMEOUT;
    chSchReadyI(tp);
  }
  chSysUnlockFromIsr();
}

/*===========================================================================*/
/* Driver interrupt handlers.                                                */
/*===========================================================================*/

void i2c_lld_serve_interrupt(I2CDriver *i2cp) {
  UNUSED(i2cp);
  bscdevice_t *device = i2cp->device;
  uint32_t status = device->status;

  if (status & (BSC_CLKT | BSC_ERR)) {
    // TODO set error flags
    wakeup_isr(i2cp, RDY_RESET);
  }
  else if (status & BSC_DONE) {
    while ((status & BSC_RXD) && (i2cp->rxidx < i2cp->rxbytes))
      i2cp->rxbuf[i2cp->rxidx++] = device->dataFifo;
    device->control = 0;
    device->status = BSC_CLKT | BSC_ERR | BSC_DONE;
    wakeup_isr(i2cp, RDY_OK);
  }
  else if (status & BSC_TXW) {
    while ((i2cp->txidx < i2cp->txbytes) && (status & BSC_TXD))
      device->dataFifo = i2cp->txbuf[i2cp->txidx++];
  }
  else if (status & BSC_RXR) {
    while ((i2cp->rxidx < i2cp->rxbytes) && (status & BSC_RXD))
      i2cp->rxbuf[i2cp->rxidx++] = device->dataFifo;
  }
}

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Low level I2C driver initialization.
 *
 * @notapi
 */
void i2c_lld_init(void) {
  i2c_init(&I2C0, BSC0_ADDR);
}

/**
 * @brief   Configures and activates the I2C peripheral.
 *
 * @param[in] i2cp      pointer to the @p I2CDriver object
 *
 * @notapi
 */
void i2c_lld_start(I2CDriver *i2cp) {
  /* Configuration */
  /* Set up GPIO pins for I2C */
  gpio_setmode(i2cp->config->ic_pin, GPFN_ALT0);
  gpio_setmode(i2cp->config->ic_pin + 1, GPFN_ALT0);
  i2cp->device->control |= BSC_I2CEN;
}

/**
 * @brief   Deactivates the I2C peripheral.
 *
 * @param[in] i2cp      pointer to the @p I2CDriver object
 *
 * @notapi
 */
void i2c_lld_stop(I2CDriver *i2cp) {
  /* Set GPIO pin function to default */
  gpio_setmode(i2cp->config->ic_pin, GPFN_IN);
  gpio_setmode(i2cp->config->ic_pin + 1, GPFN_IN);
  i2cp->device->control &= ~BSC_I2CEN;
}

/**
 * @brief   Master transmission.
 *
 * @param[in] i2cp      pointer to the @p I2CDriver object
 * @param[in] addr      slave device address (7 bits) without R/W bit
 * @param[in] txbuf     transmit data buffer pointer
 * @param[in] txbytes   number of bytes to be transmitted
 * @param[out] rxbuf     receive data buffer pointer
 * @param[in] rxbytes   number of bytes to be received
 * @param[in] timeout   the number of ticks before the operation timeouts,
 *                      the following special values are allowed:
 *                      - @a TIME_INFINITE no timeout.
 *                      .
 *
 * @notapi
 */
msg_t i2c_lld_master_transmit_timeout(I2CDriver *i2cp, i2caddr_t addr, 
                                       const uint8_t *txbuf, size_t txbytes, 
                                       uint8_t *rxbuf, const uint8_t rxbytes, 
                                       systime_t timeout) {
  VirtualTimer vt;

  /* Global timeout for the whole operation.*/
  if (timeout != TIME_INFINITE)
    chVTSetI(&vt, timeout, i2c_lld_safety_timeout, (void *)i2cp);

  i2cp->addr = addr;
  i2cp->txbuf = txbuf;
  i2cp->txbytes = txbytes;
  i2cp->txidx = 0;
  i2cp->rxbuf = rxbuf;
  i2cp->rxbytes = rxbytes;
  i2cp->rxidx = 0;

  bscdevice_t *device = i2cp->device;
  device->slaveAddress = addr;
  device->dataLength = txbytes;
  device->status = CLEAR_STATUS;

  /* Enable Interrupts and start transfer.*/
  device->control |= (BSC_INTT | BSC_INTD | START_WRITE);

  // needed? there is an outer lock already
  chSysLock();

  i2cp->thread = chThdSelf();
  chSchGoSleepS(THD_STATE_SUSPENDED);
  if ((timeout != TIME_INFINITE) && chVTIsArmedI(&vt))
    chVTResetI(&vt);

  chSysUnlock();

  return chThdSelf()->p_u.rdymsg;
}


/**
 * @brief   Master receive.
 *
 * @param[in] i2cp      pointer to the @p I2CDriver object
 * @param[in] addr      slave device address (7 bits) without R/W bit
 * @param[out] rxbuf     receive data buffer pointer
 * @param[in] rxbytes   number of bytes to be received
 * @param[in] timeout   the number of ticks before the operation timeouts,
 *                      the following special values are allowed:
 *                      - @a TIME_INFINITE no timeout.
 *                      .
 *
 * @notapi
 */
msg_t i2c_lld_master_receive_timeout(I2CDriver *i2cp, i2caddr_t addr, 
				     uint8_t *rxbuf, size_t rxbytes, 
				     systime_t timeout) {
  VirtualTimer vt;

  /* Global timeout for the whole operation.*/
  if (timeout != TIME_INFINITE)
    chVTSetI(&vt, timeout, i2c_lld_safety_timeout, (void *)i2cp);

  i2cp->addr = addr;
  i2cp->txbuf = NULL;
  i2cp->txbytes = 0;
  i2cp->txidx = 0;
  i2cp->rxbuf = rxbuf;
  i2cp->rxbytes = rxbytes;
  i2cp->rxidx = 0;

  /* Setup device.*/
  bscdevice_t *device = i2cp->device;
  device->slaveAddress = addr;
  device->dataLength = rxbytes;
  device->status = CLEAR_STATUS;

  /* Enable Interrupts and start transfer.*/
  device->control = (BSC_INTR | BSC_INTD | START_READ);

  // needed? there is an outer lock already
  chSysLock();
  i2cp->thread = chThdSelf();
  chSchGoSleepS(THD_STATE_SUSPENDED);
  if ((timeout != TIME_INFINITE) && chVTIsArmedI(&vt))
    chVTResetI(&vt);
  chSysUnlock();

  return chThdSelf()->p_u.rdymsg;
}

#endif /* HAL_USE_I2C */

/** @} */
