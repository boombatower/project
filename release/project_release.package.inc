<?php
// $Id$

/**
 * @file
 * Provides storage functions for use during release packaging.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Clear the list of components and dependencies for a release.
 *
 * @param $release_nid
 *   The project release ID.
 */
function project_release_info_package_clear($release_nid) {
  db_query('DELETE FROM {project_release_dependency}
    WHERE source_id IN (SELECT component_id FROM {project_release_component} WHERE release_nid = %d)', $release_nid);

  db_query('DELETE FROM {project_release_component} WHERE release_nid = %d', $release_nid);
}

/**
 * Store the list of components for a release.
 *
 * @param $release_nid
 *   The project release ID.
 * @param $info
 *   Associative array of information for component keyed by the component name
 *   and providing the following keys: name, title, description.
 *
 * @return
 *   Associative array of stored records keyed by the component name with all
 *   the previous keys and the generated component_id.
 */
function project_release_info_package_list_store($release_nid, array $info) {
  $records = array();
  foreach ($info as $component => $component_info) {
    $info = array(
      'release_nid' => $release_nid,
      'name' => $component_info['name'],
      'title' => $component_info['title'],
      'description' => $component_info['description'],
    );
    drupal_write_record('project_release_component', $info);
    $records[$component] = $info;
  }
  return $records;
}

/**
 * Store a list of dependencies for a component.
 *
 * @param $component_id
 *   The component ID to which the dependencies relate.
 * @param array $dependencies
 *   List of dependencies component IDs.
 */
function project_release_info_package_dependencies_store($component_id, array $dependencies) {
  foreach ($dependencies as $dependency_id) {
    // Store the dependency if a best release was found.
    $info = array(
      'source_id' => $component_id,
      'target_id' => $dependency_id,
    );
    drupal_write_record('project_release_dependency', $info);
  }
}
