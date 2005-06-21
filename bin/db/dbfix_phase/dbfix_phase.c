/*
 * Copyright (c) 2004 Nikolaus Horn <Nikolaus.Horn@zamg.ac.at All rights
 * reserved.
 * 
 * This softwarew cen be used freely in any way as long as the copyright
 * statement above is included
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "tt.h"
#include "coords.h"
#include "db.h"
#include "stock.h"

#define VERSION "1.0 2004-06-13"
#define MAX_DT 20.0

static void
usage()
{
	char           *version = VERSION;
	cbanner(version,
		"[-v] [-dry] [-pf pfname] [-sitedb sitedb]\n\t\t[-use_assoc_vmodel] [-use_iphase] [-override_phase]\n\t\tdb [orid]",
		"Nikolaus Horn",
		"ZAMG/Vienna (2004)",
		"nikolaus.horn@zamg.ac.at");
	exit(1);
}

int
main(int argc, char **argv)
{
	int             c, errflg = 0;
	Dbptr           db, dbs, dbts, t_dbassoc, dbp, dbassoc, dborigin,
	                dbarrival, dbg, dbb;
	char           *dbname = NULL, *sitedbname = NULL;
	int             nphases;
	Hook           *hook = NULL, *tthook = NULL, *ahook = NULL;
	int             norigins, nos, from, to, recc;
	char           *pfname = NULL;
	int             orid;
	double          lat, lon, depth, time, atime, stalat, stalon, staelev,
	                timeres;
	Tbl            *stbl, *ttimes = NULL;
	char            sta[10], iphase[10], phase[10], n_iphase[10];
	char           *t;
	Tbl            *stalist, *assoclist;
	int             mode = 0, result;
	char           *method = malloc(20), *model = malloc(20);
	char           *phases = malloc(20), *suspicious_phase_codes = malloc(256);
	TTGeometry      geometry;
	double          max_dt, max_tdelta;
	Pf             *pf = NULL;
	int             use_assoc_vmodel = 0, use_iphase = 0, override_phase = 0, verbose = 0,
	                quiet = 0, dry_run = 0, res, apply_station_corrections = 0,
	                input_is_view = 0;

	elog_init(argc, argv);
	elog_notify(0, "%s %s\n", Program_Name, VERSION);



	/*
	 * if (pfread (Program_Name, &pf) != 0) die (0, "Can't read parameter
	 * file\n");
	 */

	for (argv++, argc--; argc > 0; argv++, argc--) {
		if (**argv != '-')
			break;
		if (!strcmp(*argv, "-"))
			break;
		if (!strcmp(*argv, "-pf")) {
			argv++;
			argc--;
			if (argc < 1) {
				complain(0, "Need argument for -pf\n");
				usage();
			}
			strcpy(pfname, *argv);
		} else if (!strcmp(*argv, "-sitedb")) {
			argv++;
			argc--;
			if (argc < 1) {
				complain(0, "Need argument for -sitedb\n");
				usage();
			}
			sitedbname = *argv;
		} else if (!strcmp(*argv, "-use_assoc_vmodel")) {
			use_assoc_vmodel = 1;
		} else if (!strcmp(*argv, "-use_iphase")) {
			use_iphase = 1;
		} else if (!strcmp(*argv, "-override_phase")) {
			override_phase = 1;
		} else if (!strcmp(*argv, "-dry")) {
			dry_run = 1;
		} else if (!strcmp(*argv, "-v")) {
			verbose++;
		} else if (!strcmp(*argv, "-quiet")) {
			quiet = 1;
		} else {
			complain(0, "Unrecognized argument '%s'.\n", *argv);
			usage();
		}
	}



	if (argc < 1) {
		complain(0, "Need dbname argument.\n");
		usage();
	}
	dbname = *argv;

	orid = -1;
	argv++;
	argc--;
	if (argc > 0) {
		orid = atoi(*argv);
	}
	if (!pfname)
		pfname = strdup(Program_Name);
	pfread(pfname, &pf);
	if ((method = pfget_string(pf, "method")) == NULL) {
		elog_die(0,
		"parameter 'method' not found in the parameter file %s.pf\n",
			 pfname);
	}
	if ((model = pfget_string(pf, "default_vmodel")) == NULL) {
		elog_die(0,
			 "parameter 'default_vmodel' not found in the parameter file %s.pf\n",
			 pfname);
	}
	if ((phases = pfget_string(pf, "tt_phase_code")) == NULL) {
		elog_die(0,
			 "parameter 'tt_phase_code' not found in the parameter file %s.pf\n",
			 pfname);
	}
	if ((max_tdelta = pfget_double(pf, "max_tdelta")) == NULL) {
		elog_die(0,
			 "parameter 'max_tdelta' not found in the parameter file %s.pf\n",
			 pfname);
	}
	apply_station_corrections = pfget_boolean(pf, "apply_station_corrections");
	if (apply_station_corrections == -1) {
		mode = TT_APPLY_CORRECTIONS;
		if (verbose)
			elog_debug(0, "we will try to apply station corrections\n");
	}
	if ((suspicious_phase_codes = pfget_string(pf, "suspicious_phase_codes")) == NULL) {
		elog_die(0,
			 "parameter 'suspicious_phase_codes' not found in the parameter file %s.pf\n",
			 pfname);
	}
	if ((max_dt = pfget_double(pf, "max_tdelta")) == NULL) {
		elog_die(0,
			 "parameter 'max_tdelta' not found in the parameter file %s.pf\n",
			 pfname);
	}


	if (strcmp(dbname, "-")) {
		if (dbopen(dbname, "r+", &db) == dbINVALID) {
			complain(0, "dbopen(%s) error.\n", dbname);
			usage();
		}
		dborigin = dblookup(db, 0, "origin", 0, 0);
	} else {
		if (dbread_view(stdin, &db, NULL) != 0) {
			complain(0, "dbread_view() error.\n");
			usage();
		}
		dborigin = db;
	}
	if (sitedbname) {
		if (dbopen(sitedbname, "r", &dbs) < 0) {
			die(0, "can't open sitedb %s\n", sitedbname);
		} else {
			dbs = dblookup(dbs, 0, "site", 0, 0);
		}
	} else {
		dbs = dblookup(db, 0, "site", 0, 0);
	}

	if (orid >= 0) {
		char            expr[100];
		sprintf(expr, "orid==%d", orid);
		dborigin = dbsubset(dborigin, expr, 0);
		dbquery(dborigin, dbRECORD_COUNT, &recc);
		if (recc < 1) {
			elog_die(0, "no records left in table origin after subset '%s'\n", expr);
		}
	}
	dbassoc = dblookup(db, 0, "assoc", 0, 0);
	dbquery(dbassoc, dbTABLE_PRESENT, &res);
	if (!res) {
		elog_die(0, "table assoc must be present!\n");
	}
	dbquery(dbassoc, dbTABLE_IS_WRITABLE, &res);
	if (!res && !dry_run) {
		elog_die(0, "table assoc must be writable!\n");
	}
	dbarrival = dblookup(db, 0, "arrival", 0, 0);
	dbp = dbjoin(dborigin, dbassoc, 0, 0, 0, 0, 0);
	dbp = dbjoin(dbp, dbarrival, 0, 0, 0, 0, 0);

	dbquery(dbp, dbRECORD_COUNT, &recc);
	if (recc < 1) {
		elog_die(0, "no records left ifter joining assoc and arrival\n");
	}
	stbl = strtbl("orid", "lat", "lon", "depth", "time", "sta", 0);
	dbp = dbsort(dbp, stbl, 0, 0);
	dbg = dbgroup(dbp, stbl, 0, 0);
	freetbl(stbl, 0);
	/* number of origin-station bundles */
	/*
	 * thus, travel-times have to be calculated only once per
	 * (station,origin)
	 */
	dbquery(dbg, dbRECORD_COUNT, &nos);

	/* prepare for dbmatches */
	/* site */
	stbl = strtbl("sta", 0);
	/*dbts = dbs; better a fresh database, maybe the scratch record of the site table would be needed elsewhere*/
	dbts = dbtmp("css3.0");
	dbts = dblookup(dbts, 0, "site", 0, 0);
	dbts.record = dbSCRATCH;

	/* assoc */
	assoclist = strtbl("orid", "arid", 0);
	t_dbassoc = dbassoc;
	t_dbassoc.record = dbSCRATCH;

	for (dbg.record = 0; dbg.record < nos; dbg.record++) {
		dbgetv(dbg, 0, "orid", &orid,
		       "lat", &lat, "lon", &lon,
		 "depth", &depth, "time", &time, "sta", sta, "bundle", &dbb,
		       0);
		/*
		 * dbts= dbs; dbts.record=dbSCRATCH; hook=NULL;
		 * stbl=strtbl("sta",0); freetbl(stbl,0); free_hook(&hook);
		 */
		dbputv(dbts, 0, "sta", sta, 0);
		dbmatches(dbts, dbs, &stbl, &stbl, &hook, &stalist);
		if (maxtbl(stalist) > 0) {
			/*
			 * just take the 1st record, wrongly assume stations
			 * did not move around...
			 */
			dbs.record = (int) gettbl(stalist, 0);
			dbgetv(dbs, 0, "lat", &stalat, "lon", &stalon, "elev", &staelev,
			       0);
			freetbl(stalist, 0);

		} else {
			if (!quiet)
				elog_complain(0, "sta %s not found in sitetable\n", sta);
			freetbl(stalist, 0);
			continue;
			/*
			 * of course we could use delta etc. from assoc, but
			 * we want to save time and hey, where else than from
			 * a database we have that information?
			 */
		}
		geometry.receiver.lat = stalat;
		geometry.receiver.lon = stalon;
		geometry.receiver.z = -staelev;
		geometry.receiver.time = 0.0;
		strcpy(geometry.receiver.name, sta);

		geometry.source.lat = lat;
		geometry.source.lon = lon;
		geometry.source.z = depth;
		geometry.source.time = 0.0;
		strcpy(geometry.source.name, "SOURCE");

		result =
			ttcalc(method, model, phases, mode, &geometry, &ttimes, &tthook);
		if (result < 0) {
			elog_die(1,
				 "problem computing travel-times using vmodel %s->aborting\n",
				 model);
		}
		elog_clear();

		dbget_range(dbb, &from, &to);
		for (dbp.record = from; dbp.record < to; dbp.record++) {
			TTTime         *t_atime;
			int             nphases, i;
			double          dt, min_dt;
			char            niphase[20];
			char            vmodel[20];
			int             arid;
			Tbl            *assoclist;
			int             t_arid, t_orid;
			char            t_sta[10];


			dbgetv(dbp, 0, "arid", &arid, "phase", phase, "iphase", iphase,
			       "timeres", &timeres, "arrival.time", &atime,
			       "vmodel", vmodel, 0);

			if (!override_phase) {
				if (!strcontains(suspicious_phase_codes, phase, 0, 0, 0)) {
					if (verbose > 1) {
						elog_debug(0,
							   "%s %s keep phase %s for arid %d\n",
							   t=strtime(atime), sta, phase, arid);
						free(t);
					} else if (verbose) {
						elog_debug(0,
							   "keep phase %s for arid %d because it's not suspicious\n",
							   phase, arid );
						free(t);
					}
					break;
				}
			}
			if (use_assoc_vmodel) {
				result =
					ttcalc(method, vmodel, phases, mode, &geometry, &ttimes,
					       &tthook);
				if (result < 0) {
					/*
					 * fix later, allow maybe to use
					 * default vmodel...
					 */
					elog_die(1,
						 "problem computing travel-times using vmodel %s->aborting\n",
						 vmodel);
				}
				elog_clear();
			}
			nphases = maxtbl(ttimes);
			min_dt = 1e9;
			for (i = 0; i < nphases; i++) {
				t_atime = (TTTime *) gettbl(ttimes, i);
				dt = fabs(atime - (time + t_atime->value) - timeres);

				if (verbose > 2) {
					elog_debug(0, "\tttimes: %s %s\n",
						   t_atime->phase,
						   t = strtime(t_atime->value + time));
					free(t);
				}
				if (use_iphase &&
				    !strcontains(suspicious_phase_codes, t_atime->phase, 0, 0, 0)
				    && strcmp(t_atime->phase, iphase) == 0) {
					/*min_dt = dt; need to make it small for later checks*/
					min_dt=1e-9;
					strcpy(n_iphase, t_atime->phase);
					if (verbose > 1) {
						elog_debug(0, "%s %s changing phase %s to iphase %s for arid %d\n",
							   t=strtime(atime),sta,phase, n_iphase, arid);
						free(t);
					} else if (verbose) {
						elog_debug(0, "changing phase %s to iphase %s for arid %d\n",
							   phase, n_iphase, arid);
					}
					break;
				}
				if (dt < min_dt) {
					min_dt = dt;
					strcpy(n_iphase, t_atime->phase);
				}
			}

			dbputv(t_dbassoc, 0, "arid", arid, "orid", orid, 0);
			dbmatches(t_dbassoc, dbassoc, &assoclist, &assoclist, &ahook, &assoclist);
			dbassoc.record = (int) gettbl(assoclist, 0);
			dbgetv(dbassoc, 0, "orid", &t_orid, "arid", &t_arid, "sta",
			       t_sta, 0);
			freetbl(assoclist, 0);

			if (max_tdelta != -1 && min_dt > max_tdelta) {
				if (verbose > 1)
					elog_debug(0,"%s %s leave phase %s unchanged for arid %d because residual is too high (%7.3f > %7.3f)\n",t=strtime(atime),sta,phase,arid,min_dt,max_tdelta);
				free(t);
				break;
			}
			if (verbose || dry_run) {
				if (strcmp(n_iphase, iphase) != 0) {
					if (verbose > 1) {
					elog_debug(0, "%s %s changing phase %s to %s for arid %d\n",
						   t=strtime(atime),sta,phase, n_iphase, arid);
					free(t);
					} else {
					elog_debug(0, "changing phase %s to %s for arid %d\n",
						   phase, n_iphase, arid);
					}
				} else {
					if (verbose > 1) {
					elog_debug(0, "%s %s found same for phase %s arid= %d\n",
						   t=strtime(atime),sta,phase, arid);
					free(t);
					}	

				}
			}
			if (!dry_run && strcmp(n_iphase, iphase) != 0) {
				if (dbputv(dbassoc, 0, "phase", n_iphase, 0)) {
					elog_complain(1, "can't put phase %s (%s) for arid %d (sta %s @ %s) \n",
						      n_iphase, iphase, sta, t = strtime(atime));
					free(t);
				}
			}
			/*
			 * }
			 */
		}
	}
	return (0);
}