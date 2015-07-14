/***
 * AreaHandler inserts all found polygons to polygons table.
 */

#ifndef AREAHANDLER_HPP_

#include <iostream>
#include <osmium/handler.hpp>
#include <osmium/geom/ogr.hpp>

#define AREAHANDLER_HPP_

class AreaHandler: public osmium::handler::Handler {

    DataStorage &ds;

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

public:

    AreaHandler(DataStorage &data_storage) :
            ds(data_storage) {
    }

    void area(const osmium::Area& area) {
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
        }

    }
};

#endif /* AREAHANDLER_HPP_ */
