#include "cell.h"
#include "space.h"

#ifndef SWIFT_ZOOM_H
#define SWIFT_ZOOM_H

void zoom_region_init(struct swift_params *params, struct space *s);
int cell_getid_zoom(const int cdim[3], const double x, const double y,
                    const double z, const struct space *s,
                    const int i, const int j, const int k);
void construct_zoom_region(struct space *s, int verbose);
void construct_tl_cells_with_zoom_region(struct space *s, const int *cdim, const float dmin, 
        const integertime_t ti_current, const int verbose);
void find_neighbouring_cells(struct space *s, const int verbose);

#endif /* SWIFT_ZOOM_H */