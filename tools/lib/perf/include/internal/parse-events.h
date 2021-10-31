/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_PARSE_EVENTS_H
#define __LIBPERF_PARSE_EVENTS_H

struct event_symbol {
	const char	*symbol;
	const char	*alias;
};
extern struct event_symbol event_symbols_hw[];
extern struct event_symbol event_symbols_sw[];

#endif /* __LIBPERF_PARSE_EVENTS_H */
