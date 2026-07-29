// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nintendo Wii Hollywood OHCI scheduling workarounds.
 *
 * Copyright (C) 2009 The GameCube Linux Team
 * Copyright (C) 2009 Albert Herranz
 * Copyright (C) 2026 Anolis
 */

#include <linux/iopoll.h>

static DEFINE_SPINLOCK(hlwd_control_lock);

void ohci_hlwd_control_quirk(struct ohci_hcd *ohci)
{
	struct ed *ed = ohci->hlwd_control_ed;
	struct td *td;
	unsigned long flags;
	u32 current;
	u32 head;

	/*
	 * Hollywood stops accepting new TDs on a control ED after completing
	 * a transfer. Run an empty control list between requests to reset that
	 * internal state.
	 */
	if (!ed) {
		ed = ed_alloc(ohci, GFP_ATOMIC);
		if (!ed)
			return;

		td = td_alloc(ohci, GFP_ATOMIC);
		if (!td) {
			ed_free(ohci, ed);
			return;
		}

		ed->hwNextED = 0;
		ed->hwTailP = cpu_to_hc32(ohci, td->td_dma & ED_MASK);
		ed->hwHeadP = ed->hwTailP;
		ed->hwINFO = cpu_to_hc32(ohci, ED_OUT);
		ed->dummy = td;
		/* Publish the complete empty descriptor before hardware sees it. */
		wmb();

		ohci->hlwd_control_td = td;
		ohci->hlwd_control_ed = ed;
	}

	spin_lock_irqsave(&hlwd_control_lock, flags);
	head = ohci_readl(ohci, &ohci->regs->ed_controlhead);
	if (head) {
		ohci_writel(ohci, ed->dma, &ohci->regs->ed_controlhead);
		ohci_writel(ohci, ohci->hc_control | OHCI_CTRL_CLE,
			    &ohci->regs->control);
		ohci_writel(ohci, OHCI_CLF, &ohci->regs->cmdstatus);

		read_poll_timeout_atomic(ohci_readl, current, !current,
					 1, 10, false, ohci,
					 &ohci->regs->ed_controlcurrent);

		ohci_writel(ohci, ohci->hc_control, &ohci->regs->control);
		ohci_writel(ohci, head, &ohci->regs->ed_controlhead);
	}
	spin_unlock_irqrestore(&hlwd_control_lock, flags);
}

void ohci_hlwd_bulk_quirk(struct ohci_hcd *ohci)
{
	/* Also needed for Hollywood interrupt-list reliability. */
	udelay(250);
}

void ohci_hlwd_cleanup(struct ohci_hcd *ohci)
{
	if (ohci->hlwd_control_ed) {
		ed_free(ohci, ohci->hlwd_control_ed);
		ohci->hlwd_control_ed = NULL;
	}
	if (ohci->hlwd_control_td) {
		td_free(ohci, ohci->hlwd_control_td);
		ohci->hlwd_control_td = NULL;
	}
}
