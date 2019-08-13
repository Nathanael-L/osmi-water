/***
 * AreaHandler inserts all found polygons to polygons table.
 */

#ifndef AREAHANDLER_HPP_
#define AREAHANDLER_HPP_

#include <iostream>
#include <osmium/handler.hpp>
#include <osmium/geom/ogr.hpp>
#include <osmium/geom/geos.hpp>
#include <osmium_geos_factory/geos_factory.hpp>
#include <geos/simplify/TopologyPreservingSimplifier.h>
#include <geos/geom/prep/PreparedPolygon.h>


typedef geos::geom::prep::PreparedPolygon prepared_polygon_type;

class AreaHandler: public osmium::handler::Handler {

    DataStorage &ds;
    int count_polygons = 0;

    bool is_valid(const osmium::Area& area) {
        return TagCheck::is_water_area(area);
    }

    void error_message(const osmium::Area &area) {
        std::cerr << "AreaHandler: Error at ";
        if (area.from_way())
            std::cerr << "way: ";
        else
            std::cerr << "relation: ";
        std::cerr << area.orig_id() << '\n';
    }

    void insert_in_polygon_tree(const osmium::Area &area) {
        osmium_geos_factory::GEOSFactory<> osmium_geos_factory;
        geos::geom::MultiPolygon *geos_multipolygon;
        try {
            geos_multipolygon = osmium_geos_factory.create_multipolygon(area)
                                            .release();
        } catch (...) {
            error_message(area);
            std::cerr << "While init polygon_tree.\n";
            return;
        }

        for (auto geos_polygon : *geos_multipolygon) {
            prepared_polygon_type *prepared_polygon;
            try {
                prepared_polygon = new prepared_polygon_type(geos_polygon);
            } catch (...) {
                error_message(area);
                std::cerr << "While init polygon_tree.\n";
                continue;
            }
            const geos::geom::Envelope *envelope;
            envelope = geos_polygon->getEnvelopeInternal();
            ds.polygon_tree.insert(envelope, prepared_polygon);
            count_polygons++;
            ds.prepared_polygon_set.insert(prepared_polygon); 
        }
        ds.multipolygon_set.insert(geos_multipolygon);
    }

public:

    AreaHandler(DataStorage &data_storage) :
            ds(data_storage) {
    }
    
    void complete_polygon_tree() {
        if (count_polygons == 0) {
            geos::geom::GeometryFactory::unique_ptr geos_factory = geos::geom::GeometryFactory::create();
            geos::geom::Point *point;
            geos::geom::Coordinate coord(0, 0);
            point = geos_factory->createPoint(coord);
            ds.polygon_tree.insert(point->getEnvelopeInternal(), nullptr);
        }
    }

    void area(const osmium::Area &area) {
        osmium::geom::OGRFactory<> ogr_factory;
        if (!is_valid(area)) {
            return;
        }
        try {
            ds.insert_polygon_feature(ogr_factory.create_multipolygon(area), area);
            if (TagCheck::is_area_to_analyse(area)) {
                insert_in_polygon_tree(area);
            }
        } catch (osmium::geometry_error&) {
            error_message(area);
        } catch (...) {
            error_message(area);
            std::cerr << "Unexpected error\n";
        }
    }
};

#endif /* AREAHANDLER_HPP_ */
