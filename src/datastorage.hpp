/***
 * Stores all important Data over the runtime and handle the database.
 */

#ifndef DATASTORAGE_HPP_
#define DATASTORAGE_HPP_

#include <math.h>
#include <geos/index/strtree/STRtree.h>
#include <geos/index/ItemVisitor.h>
#include <geos/geom/prep/PreparedPolygon.h>

#include <memory>
#include <vector>

#include <gdalcpp.hpp>

typedef osmium::index::map::Dummy<osmium::unsigned_object_id_type,
                                  osmium::Location>
        index_neg_type;

typedef osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type,
                                           osmium::Location>
        index_pos_type;

typedef osmium::handler::NodeLocationsForWays<index_pos_type, 
                                              index_neg_type>
        location_handler_type;

//struct OGRGeometryDeleter {
//  void operator()(OGRGeometry* geom) const {
//    OGRGeometryFactory::destroyGeometry(geom);
//  }
//};

class DataStorage {
    std::string output_filename;
    osmium::geom::OGRFactory<> m_ogr_factory;
    std::unique_ptr<gdalcpp::Dataset> m_data_source;
    std::unique_ptr<gdalcpp::Layer> m_layer_polygons;
    std::unique_ptr<gdalcpp::Layer> m_layer_relations;
    std::unique_ptr<gdalcpp::Layer> m_layer_ways;
    std::unique_ptr<gdalcpp::Layer> m_layer_nodes;

    /***
     * Structure to remember the waterways according to the firstnodes and
     * lastnodes of the waterways.
     *
     * Categories are:
     *  drain, brook, ditch = A
     *  stream              = B
     *  river               = C
     *  other, canal        = ?
     *  >> ignore canals, because can differ in floating direction and size
     */
    struct WaterWay {
        osmium::object_id_type first_node;
        osmium::object_id_type last_node;
        std::string name;
        char category;

        WaterWay(osmium::object_id_type first_node,
                 osmium::object_id_type last_node,
                 const std::string name, const std::string& type) :
                 first_node(first_node),
                 last_node(last_node),
                 name(name) {
            if (type == "drain" || type == "brook" || type == "ditch") {
                category = 'A';
            } else if (type == "stream") {
                category = 'B';
            } else if (type == "river") {
                category = 'C';
            } else {
                category = '?';
            }
        }
    };

    std::vector<WaterWay*> waterways;

    void init_db() {
        CPLSetConfigOption("OGR_SQLITE_PRAGMA", "journal_mode=OFF,TEMP_STORE=MEMORY,temp_store=memory,LOCKING_MODE=EXCLUSIVE");
        CPLSetConfigOption("OGR_SQLITE_CACHE", "600");
        CPLSetConfigOption("OGR_SQLITE_JOURNAL", "OFF");
        CPLSetConfigOption("OGR_SQLITE_SYNCHRONOUS", "OFF");

        m_data_source = std::unique_ptr<gdalcpp::Dataset>{new gdalcpp::Dataset("SQlite", output_filename, gdalcpp::SRS(4326), {"SPATIALITE=YES"})};
        m_layer_polygons = std::unique_ptr<gdalcpp::Layer>{new gdalcpp::Layer(*m_data_source, "polygons", wkbMultiPolygon, {"SPATIAL_INDEX=NO", "COMPRESS_GEOM=NO"})};
        m_layer_relations = std::unique_ptr<gdalcpp::Layer>{new gdalcpp::Layer(*m_data_source, "relations", wkbMultiLineString, {"SPATIAL_INDEX=NO", "COMPRESS_GEOM=NO"})};
        m_layer_ways = std::unique_ptr<gdalcpp::Layer>{new gdalcpp::Layer(*m_data_source, "ways", wkbLineString, {"SPATIAL_INDEX=NO", "COMPRESS_GEOM=NO"})};
        m_layer_nodes = std::unique_ptr<gdalcpp::Layer>{new gdalcpp::Layer(*m_data_source, "nodes", wkbPoint, {"SPATIAL_INDEX=NO", "COMPRESS_GEOM=NO"})};

        /*---- TABLE POLYGONS ----*/
        m_layer_polygons->add_field("way_id", OFTInteger, 12);
        m_layer_polygons->add_field("relation_id", OFTInteger, 12);
        m_layer_polygons->add_field("type", OFTString, 10);
        m_layer_polygons->add_field("name", OFTString, 30);
        m_layer_polygons->add_field("lastchange", OFTString, 20);
        m_layer_polygons->add_field("error", OFTString, 6);

        /*---- TABLE RELATIONS ----*/
        m_layer_relations->add_field("relation_id", OFTInteger, 12);
        m_layer_relations->add_field("type", OFTString, 10);
        m_layer_relations->add_field("name", OFTString, 30);
        m_layer_relations->add_field("lastchange", OFTString, 20);
        m_layer_relations->add_field("nowaterway_error", OFTString, 6);
        m_layer_relations->add_field("tagging_error", OFTString, 6);

        /*---- TABLE WAYS ----*/
        m_layer_ways->add_field("way_id", OFTInteger, 12);
        m_layer_ways->add_field("type", OFTString, 10);
        m_layer_ways->add_field("name", OFTString, 30);
        m_layer_ways->add_field("firstnode", OFTString, 11);
        m_layer_ways->add_field("lastnode", OFTString, 11);
        m_layer_ways->add_field("relation_id", OFTInteger, 10);
        m_layer_ways->add_field("width", OFTString, 10);
        m_layer_ways->add_field("lastchange", OFTString, 20);
        m_layer_ways->add_field("construction", OFTString, 7);
        m_layer_ways->add_field("width_error", OFTString, 6);
        m_layer_ways->add_field("tagging_error", OFTString, 6);

        /*---- TABLE NODES ----*/
        m_layer_nodes->add_field("node_id", OFTString, 12);
        m_layer_nodes->add_field("specific", OFTString, 11);
        m_layer_nodes->add_field("direction_error", OFTString, 6);
        m_layer_nodes->add_field("name_error", OFTString, 6);
        m_layer_nodes->add_field("type_error", OFTString, 6);
        m_layer_nodes->add_field("spring_error", OFTString, 6);
        m_layer_nodes->add_field("end_error", OFTString, 6);
        m_layer_nodes->add_field("way_error", OFTString, 6);
    }

    const std::string get_timestamp(osmium::Timestamp timestamp) {
        std::string time_str = timestamp.to_iso();
        time_str.replace(10, 1, " ");
        time_str.replace(19, 1, "");
        return time_str;
    }

    /***
     * Get width as float in meter from the common formats. Detect errors
     * within the width string.
     * A ',' as separator dedicates an erroror, but is handled.
     */
    bool get_width(const char *width_chr, float &width) {
        if (!width_chr) {
            width = 0;
            return false;
        }
        std::string width_str = width_chr;
        bool error = false;

        if (width_str.find(",") != std::string::npos) {
            width_str.replace(width_str.find(","), 1, ".");
            error = true;
            width_chr = width_str.c_str();
        }

        char *endptr;
        width = strtof(width_chr, &endptr);

        if (endptr == width_chr) {
            width = -1;
        } else if (*endptr) {
            while(isspace(*endptr)) endptr++;
            if (!strcasecmp(endptr, "m")) {
            } else if (!strcasecmp(endptr, "km")) {
                width *= 1000;
            } else if (!strcasecmp(endptr, "mi")) {
                width *= 1609.344;
            } else if (!strcasecmp(endptr, "nmi")) {
                width *= 1852;
            } else if (!strcmp(endptr, "'")) {
                width *= 12.0 * 0.0254;
            } else if (!strcmp(endptr, "\"")) {
                width *= 0.0254;
            } else if (*endptr == '\'') {
                endptr++;
                char *inchptr;
                float inch = strtof(endptr, &inchptr);
                if ((!strcmp(inchptr, "\"")) && (endptr != inchptr)) {
                    width = (width * 12 + inch) * 0.0254;
                } else {
                    width = -1;
                    error = true;
                }
            } else {
                width = -1;
                error = true;
            }
        }
        return error;
    }

    std::string width2string(float &width) {
        int rounded_width = static_cast<int> (round(width * 10));
        std::string width_str = std::to_string(rounded_width);
        if (width_str.length() == 1) {
            width_str.insert(width_str.begin(), '0');
        }
        width_str.insert(width_str.end() - 1, '.');
        return width_str;
    }

    void remember_way(osmium::object_id_type first_node,
                      osmium::object_id_type last_node,
                      const std::string name, const std::string& type) {
        WaterWay *wway = new WaterWay(first_node, last_node, std::move(name), type);
        waterways.push_back(wway);
        node_map[first_node].push_back(wway);
        node_map[last_node].push_back(wway);
    }

    void destroy_polygons() {
        for (auto polygon : prepared_polygon_set) {
            delete polygon;
        }
        for (auto multipolygon : multipolygon_set) {
            delete multipolygon;
        }
    }

public:
    /***
     * node_map: Contains all first_nodes and last_nodes of found waterways with
     * the names and categories of the connected ways.
     * error_map: Contains ids of the potential error nodes (or mouths) to be
     * checked in pass 3.
     * error_tree: The potential error nodes remaining after pass 3 are stored
     * in here for a geometrical analysis in pass 5.
     * polygon_tree: contains prepared polygons of all water polygons except of
     * riverbanks found in pass 4. 
     */
    google::sparse_hash_map<osmium::object_id_type, std::vector<WaterWay*>> node_map;
    google::sparse_hash_map<osmium::object_id_type, ErrorSum*> error_map;
//    geos::index::strtree::STRtree error_tree;
    google::sparse_hash_set<geos::geom::prep::PreparedPolygon*> prepared_polygon_set;
    google::sparse_hash_set<geos::geom::MultiPolygon*> multipolygon_set;
    geos::index::strtree::STRtree polygon_tree;

    explicit DataStorage(std::string outfile) :
            output_filename(outfile),
            m_ogr_factory() {
        init_db();
        node_map.set_deleted_key(-1);
        error_map.set_deleted_key(-1);
        prepared_polygon_set.set_deleted_key(nullptr);
        multipolygon_set.set_deleted_key(nullptr);
    }

    ~DataStorage() {
        destroy_polygons();
        for (auto wway : waterways) {
            delete wway;
        }
    }

    void insert_polygon_feature(std::unique_ptr<OGRMultiPolygon>&& geom, const osmium::Area &area) {
        osmium::object_id_type way_id;
        osmium::object_id_type relation_id;
        if (area.from_way()) {
            way_id = area.orig_id();
            relation_id = 0;
        } else {
            way_id = 0;
            relation_id = area.orig_id();
        }

        const std::string type = TagCheck::get_polygon_type(area);
        const char* name = area.get_value_by_key("name");

        try {
            gdalcpp::Feature feature(*m_layer_polygons, std::move(geom));
            feature.set_field("way_id", static_cast<int>(way_id));
            feature.set_field("relation_id", static_cast<int>(relation_id));
            feature.set_field("type", type.c_str());
            if (name) {
                feature.set_field("name", name);
            }
            feature.set_field("lastchange",
                              get_timestamp(area.timestamp()).c_str());
            feature.add_to_layer();
        } catch (osmium::geometry_error& err) {
            std::cerr << "Failed to create geometry feature for polygon of ";
            if (area.from_way()) std::cerr << "way: ";
            else std::cerr << "relation: ";
            std::cerr << area.orig_id() << '\n';
        }
    }

    void insert_relation_feature(std::unique_ptr<OGRGeometry>&& geom,
                                 const osmium::Relation &relation,
                                 bool contains_nowaterway) {
        const std::string type = TagCheck::get_way_type(relation);
        const char *name = relation.get_value_by_key("name");

        try {
            gdalcpp::Feature feature(*m_layer_relations, std::move(geom));
            feature.set_field("relation_id", static_cast<int>(relation.id()));
            feature.set_field("type", type.c_str());
            if (name) {
                feature.set_field("name", name);
            }
            feature.set_field("lastchange",
                              get_timestamp(relation.timestamp()).c_str());
            if (contains_nowaterway)
                feature.set_field("nowaterway_error", "true");
            else
                feature.set_field("nowaterway_error", "false");
            feature.add_to_layer();
        } catch (osmium::geometry_error& err) {
            std::cerr << "Failed to create relation feature:" << err.what() << "\n";
        }
    }

    void insert_way_feature(std::unique_ptr<OGRGeometry>&& geom,
                            const osmium::Way &way,
                            osmium::object_id_type rel_id) {
        const std::string type = TagCheck::get_way_type(way);
        const char *width = TagCheck::get_width(way);
        const std::string construction = TagCheck::get_construction(way);
        const std::string name {way.get_value_by_key("name", "")};

        bool width_err;
        float w = 0;
        width_err = get_width(width, w);

        char first_node_chr[13], last_node_chr[13];
        osmium::object_id_type first_node = way.nodes().cbegin()->ref();
        osmium::object_id_type last_node = way.nodes().crbegin()->ref();
        sprintf(first_node_chr, "%ld", first_node);
        sprintf(last_node_chr, "%ld", last_node);

        try {
            gdalcpp::Feature feature(*m_layer_ways, std::move(geom));
            feature.set_field(0, static_cast<int>(way.id()));
            feature.set_field(1, type.c_str());
            if (!name.empty()) {
                feature.set_field(2, name.c_str());
            }
            feature.set_field(3, first_node_chr);
            feature.set_field(4, last_node_chr);
            feature.set_field(5, static_cast<int>(rel_id));
            feature.set_field("lastchange", get_timestamp(way.timestamp()).c_str());
            feature.set_field("construction", construction.c_str());
            feature.set_field("width_error", (width_err) ? "true" : "false");
            feature.add_to_layer();
        } catch (osmium::geometry_error& err) {
            std::cerr << "Failed to create geometry feature for way: "
                 << way.id() << '\n';
        }

        remember_way(first_node, last_node, std::move(name), type);
    }

    void insert_node_feature(osmium::Location location,
                             osmium::object_id_type node_id,
                             ErrorSum *sum) {
        std::unique_ptr<OGRPoint> point;
        try {
            point = m_ogr_factory.create_point(location);
        } catch (osmium::geometry_error&) {
            std::cerr << "Error at node: " << node_id << '\n';
            return;
        } catch (...) {
            std::cerr << "Error at node: " << node_id << '\n'
                 << "Unexpected error" << '\n';
            return;
        }

        gdalcpp::Feature feature {*m_layer_nodes, std::move(point)};

        char id_chr[12];
        sprintf(id_chr, "%ld", node_id);

        feature.set_field("node_id", id_chr);
        if (sum->is_rivermouth()) feature.set_field("specific", "rivermouth");
        else feature.set_field("specific", (sum->is_outflow()) ? "outflow": "");
        feature.set_field("direction_error",
                          (sum->is_direction_error()) ? "true" : "false");
        feature.set_field("name_error",
                          (sum->is_name_error()) ? "true" : "false");
        feature.set_field("type_error",
                          (sum->is_type_error()) ? "true" : "false");
        feature.set_field("spring_error",
                          (sum->is_spring_error()) ? "true" : "false");
        feature.set_field("end_error",
                          (sum->is_end_error()) ? "true" : "false");
        feature.set_field("way_error",
                          (sum->is_way_error()) ? "true" : "false");
        feature.add_to_layer();

    }

//    /***
//     * Insert the error nodes remaining after first indicate false positives
//     * in pass 3 into the error_tree.
//     * FIXME: memory for point isn't free'd
//     */
//    void init_tree(location_handler_type &location_handler) {
//        osmium::geom::GEOSFactory<> geos_factory;
//        geos::geom::Point *point;
//        for (auto& node : error_map) {
//            if (!(node.second->is_rivermouth()) &&
//                  (!(node.second->is_outflow()))) {
//                point = geos_factory.create_point(
//                        location_handler.get_node_location(node.first))
//                        .release();
//                error_tree.insert(point->getEnvelopeInternal(),
//                                  (osmium::object_id_type *) &(node.first));
//            }
//        }
//        if (error_map.size() == 0) {
//            geos::geom::GeometryFactory org_geos_factory;
//            geos::geom::Coordinate coord(0, 0);
//            point = org_geos_factory.createPoint(coord);
//            error_tree.insert(point->getEnvelopeInternal(), 0);
//        }
//    }

    /***
     * Insert the error nodes into the nodes table.
     */
    void insert_error_nodes(location_handler_type &location_handler) {
        osmium::Location location;
        for (auto node : error_map) {
            node.second->switch_poss();
            osmium::object_id_type node_id = node.first;
            location = location_handler.get_node_location(node_id);
            insert_node_feature(location, node_id, node.second);
            delete node.second;
        }
    }
};

#endif /* DATASTORAGE_HPP_ */
