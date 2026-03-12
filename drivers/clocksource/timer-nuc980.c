// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton nuc980 Clocksource driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/sched_clock.h>

#define nuc980_TMR_CTL		0x0
#define nuc980_TMR_CMP		0x4
#define nuc980_TMR_INTSTS	0x8
#define nuc980_TMR_CNT		0xC

#define nuc980_TMR_CTL_CNTEN	BIT(30)
#define nuc980_TMR_CTL_INTEN	BIT(29)
#define nuc980_TMR_CTL_OPMODE	GENMASK(28, 27)
#define OPMODE_ONESHOT		0
#define OPMODE_PERIODIC		1
#define OPMODE_TOGGLE		2
#define OPMODE_CONTINUOUS	3
#define nuc980_TMR_CTL_ACTSTS	BIT(25)
#define nuc980_TMR_CTL_PSC		GENMASK(7, 0)
#define nuc980_TMR_CMP_MASK		GENMASK(23, 0)
#define nuc980_TMR_INTSTS_TIF	BIT(0)
#define nuc980_TMR_CNT_MASK		GENMASK(23, 0)
#define nuc980_TMR_CNT_WIDTH	24

#define to_nuc980_clk_event(c) \
	(container_of(c, struct nuc980_clock_event, ce_dev))

struct nuc980_clock_event {
	struct clock_event_device ce_dev;
	void __iomem *base;
	unsigned long rate;
	int irq;
	u32 ticks_per_jiffy;
};

static struct delay_timer nuc980_delay_timer;
static void __iomem *clocksource_timer_counter;

static u64 notrace nuc980_read_sched_clock(void)
{
	return readl(clocksource_timer_counter);
}

static unsigned long nuc980_delay_timer_read(void)
{
	return readl(clocksource_timer_counter);
}

static int __init nuc980_clocksource_init(struct device_node *np)
{
	void __iomem *base;
	unsigned long rate;
	struct clk *clk;
	u32 reg, prescale;
	int ret;

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("%pOF: failed to get clock\n", np);
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("%pOF: failed to enable clock\n", np);
		goto err_clk_enable;
	}

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%pOF: failed to map registers\n", np);
		ret = -ENOMEM;
		goto err_iomap;
	}
	writel_relaxed(63, base + nuc980_TMR_CTL);

	/* clock source in continuous mode */
	prescale = readl_relaxed(base + nuc980_TMR_CTL) & nuc980_TMR_CTL_PSC;
	reg = FIELD_PREP(nuc980_TMR_CTL_OPMODE, OPMODE_CONTINUOUS) | prescale;
	writel_relaxed((1ul << nuc980_TMR_CNT_WIDTH) - 1, base + nuc980_TMR_CMP);
	writel_relaxed(0, base + nuc980_TMR_INTSTS);
	writel_relaxed(reg , base + nuc980_TMR_CTL);	
	writel_relaxed(reg | nuc980_TMR_CTL_CNTEN, base + nuc980_TMR_CTL);

	rate = DIV_ROUND_UP(clk_get_rate(clk), prescale + 1);
	ret = clocksource_mmio_init(base + nuc980_TMR_CNT, np->name,
				    rate, 200, nuc980_TMR_CNT_WIDTH,
				    clocksource_mmio_readl_up);
	if (ret) {
		pr_err("%pOF: failed to register clocksource\n", np);
		goto err_clocksource_init;
	}

	clocksource_timer_counter = base + nuc980_TMR_CNT;
	nuc980_delay_timer.read_current_timer = nuc980_delay_timer_read;
	nuc980_delay_timer.freq = rate;
	register_current_timer_delay(&nuc980_delay_timer);
	sched_clock_register(nuc980_read_sched_clock,
			nuc980_TMR_CNT_WIDTH, rate);

	pr_info("%pOF: nuc980 sched_clock registered\n", np);

	return 0;

err_clocksource_init:
	iounmap(base);
err_iomap:
	clk_disable_unprepare(clk);
err_clk_enable:
	clk_put(clk);
	return ret;
}

static irqreturn_t nuc980_clock_event_handler(int irq, void *dev_id)
{
	struct nuc980_clock_event *priv = dev_id;

	/* Clear interrupt flag */
	writel_relaxed(nuc980_TMR_INTSTS_TIF, priv->base + nuc980_TMR_INTSTS);

	priv->ce_dev.event_handler(&priv->ce_dev);

	return IRQ_HANDLED;
}

static void nuc980_timer_start(struct nuc980_clock_event *priv)
{
	u32 reg;

	reg = readl_relaxed(priv->base + nuc980_TMR_CTL);
	reg |= nuc980_TMR_CTL_CNTEN;
	writel_relaxed(reg, priv->base + nuc980_TMR_CTL);
}

static int nuc980_clkevt_next_event(unsigned long evt,
				 struct clock_event_device *dev)
{
	struct nuc980_clock_event *priv = to_nuc980_clk_event(dev);

	writel_relaxed(evt & nuc980_TMR_CMP_MASK, priv->base + nuc980_TMR_CMP);

	nuc980_timer_start(priv);

	return 0;
}

static int nuc980_clkevt_shutdown(struct clock_event_device *dev)
{
	struct nuc980_clock_event *priv = to_nuc980_clk_event(dev);
	u32 reg;

	reg = readl_relaxed(priv->base + nuc980_TMR_CTL);
	reg &= ~(nuc980_TMR_CTL_CNTEN | nuc980_TMR_CTL_INTEN);
	writel_relaxed(reg, priv->base + nuc980_TMR_CTL);

	return 0;
}

static int nuc980_clkevt_oneshot(struct clock_event_device *dev)
{
	struct nuc980_clock_event *priv = to_nuc980_clk_event(dev);
	u32 reg;

	reg = readl_relaxed(priv->base + nuc980_TMR_CTL) & nuc980_TMR_CTL_PSC;
	reg |= FIELD_PREP(nuc980_TMR_CTL_OPMODE, OPMODE_ONESHOT) |
			nuc980_TMR_CTL_INTEN;
	writel_relaxed(reg, priv->base + nuc980_TMR_CTL);

	return 0;
}

static int nuc980_clkevt_periodic(struct clock_event_device *dev)
{
	struct nuc980_clock_event *priv = to_nuc980_clk_event(dev);
	u32 reg;

	writel_relaxed(priv->ticks_per_jiffy & nuc980_TMR_CMP_MASK,
		       priv->base + nuc980_TMR_CMP);

	reg = readl_relaxed(priv->base + nuc980_TMR_CTL) & nuc980_TMR_CTL_PSC;
	reg |= FIELD_PREP(nuc980_TMR_CTL_OPMODE, OPMODE_PERIODIC) |
			nuc980_TMR_CTL_INTEN;
	writel_relaxed(reg, priv->base + nuc980_TMR_CTL);

	nuc980_timer_start(priv);

	return 0;
}

static struct nuc980_clock_event nuc980_clk_event = {
	.ce_dev = {
		.name		= "nuc980 clockevent",
		.features	= CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
		.rating		= 300,
		.set_next_event		= nuc980_clkevt_next_event,
		.set_state_shutdown	= nuc980_clkevt_shutdown,
		.set_state_oneshot	= nuc980_clkevt_oneshot,
		.set_state_periodic	= nuc980_clkevt_periodic,
	},
};

static int __init nuc980_clockevent_init(struct device_node *np)
{
	struct nuc980_clock_event *priv = &nuc980_clk_event;
	struct clk *clk;
	u32 prescale;
	int ret;

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_err("%pOF: failed to get clock\n", np);
		return PTR_ERR(clk);
	}

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("%pOF: failed to enable clock\n", np);
		return ret;
	}

	priv->base = of_iomap(np, 0);
	if (!priv->base) {
		pr_err("%pOF: failed to map registers\n", np);
		return -ENOMEM;
	}

	priv->irq = irq_of_parse_and_map(np, 0);
	if (!priv->irq) {
		pr_err("%pOF: failed to get irq\n", np);
		return -EINVAL;
	}

	ret = request_irq(priv->irq, nuc980_clock_event_handler,
			  IRQF_TIMER | IRQF_IRQPOLL, "nuc980 clockevent", priv);
	if (ret) {
		pr_err("%pOF: request irq failed\n", np);
		return ret;
	}
	writel_relaxed(63, priv->base + nuc980_TMR_CTL);

	/* reset clock event */
	prescale = readl_relaxed(priv->base + nuc980_TMR_CTL) & nuc980_TMR_CTL_PSC;
	writel_relaxed(0, priv->base + nuc980_TMR_INTSTS);
	writel_relaxed(0, priv->base + nuc980_TMR_CNT);
	writel_relaxed(prescale, priv->base + nuc980_TMR_CTL);

	priv->rate = DIV_ROUND_UP(clk_get_rate(clk), prescale + 1);
	priv->ticks_per_jiffy = DIV_ROUND_CLOSEST(priv->rate, HZ);

	clockevents_config_and_register(&priv->ce_dev,
		priv->rate, 1, (1 << nuc980_TMR_CNT_WIDTH) - 1);

	pr_info("%pOF: nuc980 clockevent registered\n", np);

	return 0;
}

static int __init nuc980_timer_init(struct device_node *np)
{
	if (of_device_is_compatible(np, "nuvoton,nuc980-clksrc")) {
		return nuc980_clocksource_init(np);
	}

	if (of_device_is_compatible(np, "nuvoton,nuc980-clkevt")) {
		return nuc980_clockevent_init(np);
	}

	return 0;
}
TIMER_OF_DECLARE(nuc980_clksrc, "nuvoton,nuc980-clksrc", nuc980_timer_init);
TIMER_OF_DECLARE(nuc980_clkevt, "nuvoton,nuc980-clkevt", nuc980_timer_init);

