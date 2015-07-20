/***
 * AreaHandler inserts all found polygons to polygons table.
 */

#ifndef AREAHANDLER_HPP_

#include <iostream>
#include <osmium/handler.hpp>
#include <osmium/geom/ogr.hpp>
#include <osmium/geom/geos.hpp>
#include <geos/simplify/TopologyPreservingSimplifier.h>
#include <geos/geom/prep/PreparedPolygon.h>

#define AREAHANDLER_HPP_

typedef geos::geom::prep::PreparedPolygon prepared_polygon_type;

class AreaHandler: public osmium::handler::Handler {

    DataStorage &ds;
    int count_polygons = 0;

    bool is_valid(const osmium::Area& area) {
        return TagCheck::is_water_area(area);
    }

    void errormsg(const osmium::Area &area) {
        cerr << "AreaHandler: Error at ";
        if (area.from_way())
            cerr << "way: ";
        else
            cerr << "relation: ";
        cerr << area.orig_id() << endl;
    }

    void insert_in_polygon_tree(const osmium::Area &area) {
        osmium::geom::GEOSFactory<> geos_factory;
        geos::geom::MultiPolygon *geos_multipolygon;
        geos_multipolygon = geos_factory.create_multipolygon(area).release();

        for (auto geos_polygon : *geos_multipolygon) {
            prepared_polygon_type *prepared_polygon;
            prepared_polygon = new prepared_polygon_type(geos_polygon);
            const geos::geom::Envelope *envelope;
            envelope = geos_polygon->getEnvelopeInternal();
            ds.polygon_tree.insert(envelope, prepared_polygon);
            ds.prepared_polygon_set.insert(prepared_polygon); 
        }
        ds.multipolygon_set.insert(geos_multipolygon);
    }

public:

    AreaHandler(DataStorage &data_storage) :
            ds(data_storage) {
    }

    void area(const osmium::Area &area) {
        osmium::geom::OGRFactory<> ogr_factory;
        if (is_valid(area)) {
            OGRMultiPolygon *geom = nullptr;
            try {
                geom = ogr_factory.create_multipolygon(area).release();
            } catch (osmium::geometry_error) {
                errormsg(area);
            } catch (...) {
                errormsg(area);
                cerr << "Unexpected error" << endl;
            }
            if (geom) {
                ds.insert_polygon_feature(geom, area);
                OGRGeometryFactory::destroyGeometry(geom);
            }
            if (TagCheck::is_area_to_analyse(area)) {
                { // Benchmark
                    t_inittree.start();
                }
                insert_in_polygon_tree(area);
                { // Benchmark
                    t_inittree.stop();
                }
            }
        }

    }
};

#endif /* AREAHANDLER_HPP_ */
