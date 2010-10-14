<?php
// $Id$

/**
 * @file
 * Provides Drupal implementation of project release info parsing and batch API.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

module_load_include('package.inc', 'project_release');

/**
 * Minimum API compatibility to process.
 */
define('PROJECT_RELEASE_INFO_MINIMUM_API', 6);

/**
 * Parse Drupal info files for project information.
 *
 * @param $rid
 *   The project release ID.
 * @param $info_files
 *   List of .info files to be processed.
 * @param $uri
 *   The short name of the project.
 * @param $version
 *   The version of the release being processed.
 */
function package_release_info_process_all($rid, array $info_files, $uri, $version) {
  // Only process .info file for modules that meet the minimum API requirement.
  if (($major = array_shift(explode('.', $version, 2))) && $major >= PROJECT_RELEASE_INFO_MINIMUM_API) {
    // Clear previous records for the release.
    project_release_info_package_clear($rid);

    $info = package_release_info_parse($info_files);

    // Store list of components.
    $records = project_release_info_package_list_store($rid, $info);

    // Process list of dependencies to determine the release related to each
    // dependency.
    package_release_info_process($rid, $records, $info);
  }
}

/**
 * Parse all the .info files.
 *
 * @param $info_files
 *   List of component .info files.
 *
 * @return
 *   Associative array of component information keyed by component name and
 *   merged with an array of defaults.
 */
function package_release_info_parse(array $info_files) {
  $defaults = array(
    'name' => 'Unknown',
    'description' => '',
    'dependencies' => array(),
    'test_dependencies' => array(),
  );

  $info = array();
  foreach ($info_files as $file) {
    $component = basename($file, '.info');
    $info[$component] = drupal_parse_info_file($file) + $defaults;

    // Change info keys to suite project_release_info.
    $info[$component]['title'] = $info[$component]['name'];
    $info[$component]['name'] = $component;
  }
  return $info;
}

/**
 * Determine and store the list of dependencies for a release.
 *
 * @param $rid
 *   The project release ID.
 * @param $records
 *   List of saved component information from
 *   project_release_info_package_list_store().
 * @param $info
 *   Associative array of information from component .info files keyed by component
 *   name and merged with a set of detaults from package_release_info_parse().
 */
function package_release_info_process($rid, array $records, array $info) {
  foreach ($info as $component => $component_info) {
    // Merge required dependencies and test dependencies into one array.
    $dependencies = array_merge($component_info['dependencies'], $component_info['test_dependencies']);
    package_release_info_process_dependencies($rid, $records[$component], $dependencies);
  }
}

/**
 * Store the list of dependencies for a component of a release.
 *
 * @param $rid
 *   The project release ID.
 * @param $component
 *   Component information, returned from project_release_info_package_list_store().
 * @param $dependencies
 *   List of dependency component names.
 */
function package_release_info_process_dependencies($rid, array $component, array $dependencies) {
  // Load the release and determine is core API version.
  $release = node_load($rid);
  if (!($api_term = package_release_info_core_api($release))) {
    wd_err('ERROR: No core release API term found.');
    return;
  }

  // Preprocess dependencies looking for duplications, or overrides. Since test
  // dependencies were appended to regular dependencies they should override.
  $dependency_info = array();
  foreach ($dependencies as $dependency) {
    $info = drupal_parse_dependency($dependency, $api_term->name);
    if (empty($info['name'])) {
      wd_err('ERROR: Invalid dependency found [' . $dependency . '] of [' . $component['name'] . '].');
      return;
    }
    $dependency_info[$info['name']] = $info;
  }

  $dependency_components = array();
  foreach ($dependency_info as $dependency => $info) {
    $releases = package_release_info_releases_get($dependency, $api_term->tid);

    // Check for dependency version restriction information.
    if (empty($info['versions'])) {
      // Cycle through the releases made by the project until a dev branch is
      // found of the latest stable series that matches the core API. The
      // releases are in descending order from the largest release version to
      // the smallest release version.
      $best_release = NULL;
      foreach ($releases as $release) {
        $release_node = node_load($release['rid']);
        if ($release_node->project_release['rebuild']) {
          // Release represents a dev branch, store it.
          $best_release = $release;
        }
        elseif ($best_release) {
          // Release represents a stable branch, since a dev branch has already
          // been found then stop and use the dev branch as the best branch.
          $dependency_components[$dependency] = $best_release['component_id'];
          break;
        }
      }

      // If no releases found, then generate error.
      if (empty($dependency_components[$dependency])) {
        wd_err('ERROR: No development release with a corresponding stable release was found that ' .
          'matches the requirements for dependency [' . $dependency . '] of [' . $component['name'] . '].');
        return;
      }
    }
    else {
      // Cycle through each release from highest version number to lowest and
      // select the first dev release that matches the criteria.
      foreach ($releases as $release) {
        $release_node = node_load($release['rid']);

        // Get the release version string without the Drupal API part.
        $parts = explode('-', $release_node->project_release['version']);
        unset($parts[0]);
        $current = implode('-', $parts);

        // Compare the release version string against the requirements.
        $valid = TRUE;
        foreach ($info['versions'] as $required) {
          if ((isset($required_version['op']) && !version_compare($required, $required['version'], $required['op']))) {
            $valid = FALSE;
            break;
          }
        }

        // If all the requirements were met then use the release.
        if ($valid) {
          $dependency_components[$dependency] = $release['component_id'];
        }
      }

      // If no releases found, then generate error.
      if (empty($dependency_components[$dependency])) {
        wd_err('ERROR: No release found that matches requirements [' . trim($info['original_version']) .
          '] for dependency [' . $dependency . '] of [' . $component['name'] . '].');
        return;
      }
    }
  }

  // Store dependencies for component.
  project_release_info_package_dependencies_store($component['component_id'], $dependency_components);
}

/**
 * Get the releases that contain a component and are compatible with an API.
 *
 * @param $component
 *   Component name.
 * @param $api_tid
 *   Core API compatibility tid.
 *
 * @return
 *   List of releases with keys: 'component_id', 'rid'.
 */
function package_release_info_releases_get($component, $api_tid) {
  $result = db_query("SELECT DISTINCT p.component_id, p.release_nid
    FROM {project_release_component} p
    INNER JOIN {project_release_nodes} r ON p.release_nid = r.nid
    INNER JOIN {term_node} t ON p.release_nid = t.nid
    WHERE t.tid = %d AND p.name = '%s'
    ORDER BY r.version DESC, t.vid DESC
  ", $api_tid, $component);

  $releases = array();
  while ($release = db_fetch_array($result)) {
    $releases[] = $release;
  }
  return $releases;
}

/**
 * Attempt to determine the Drupal core API term.
 *
 * @param $node
 *   Node object.
 *
 * @return
 *   Core API term to which the node belongs, otherwise FALSE.
 */
function package_release_info_core_api($node) {
  static $api_terms = array();

  if (!isset($api_terms[$node->nid])) {
    $api_terms[$node->nid] = FALSE;

    if (!empty($node->taxonomy)) {
      // Relase API version vocabulary.
      $api_vid = _project_release_get_api_vid();

      foreach ($node->taxonomy as $tid => $term) {
        if ($term->vid == $api_vid) {
          $api_terms[$node->nid] = $term;
        }
      }
    }
  }
  return $api_terms[$node->nid];
}

/**
 * Parse a dependency for comparison by drupal_check_incompatibility().
 *
 * @param $dependency
 *   A dependency string, for example 'foo (>=7.x-4.5-beta5, 3.x)'.
 *
 * @return
 *   An associative array with three keys:
 *   - 'name' includes the name of the thing to depend on (e.g. 'foo').
 *   - 'original_version' contains the original version string (which can be
 *     used in the UI for reporting incompatibilities).
 *   - 'versions' is a list of associative arrays, each containing the keys
 *     'op' and 'version'. 'op' can be one of: '=', '==', '!=', '<>', '<',
 *     '<=', '>', or '>='. 'version' is one piece like '4.5-beta3'.
 *   Callers should pass this structure to drupal_check_incompatibility().
 *
 * @see drupal_check_incompatibility()
 */
function drupal_parse_dependency($dependency, $core_api) {
  // We use named subpatterns and support every op that version_compare
  // supports. Also, op is optional and defaults to equals.
  $p_op = '(?P<operation>!=|==|=|<|<=|>|>=|<>)?';
  // Core version is always optional: 7.x-2.x and 2.x is treated the same.
  $p_core = '(?:' . preg_quote($core_api . '.x') . '-)?';
  $p_major = '(?P<major>\d+)';
  // By setting the minor version to x, branches can be matched.
  $p_minor = '(?P<minor>(?:\d+|x)(?:-[A-Za-z]+\d+)?)';
  $value = array();
  $parts = explode('(', $dependency, 2);
  $value['name'] = trim($parts[0]);
  if (isset($parts[1])) {
    $value['original_version'] = ' (' . $parts[1];
    foreach (explode(',', $parts[1]) as $version) {
      if (preg_match("/^\s*$p_op\s*$p_core$p_major\.$p_minor/", $version, $matches)) {
        $op = !empty($matches['operation']) ? $matches['operation'] : '=';
        if ($matches['minor'] == 'x') {
          // Drupal considers "2.x" to mean any version that begins with
          // "2" (e.g. 2.0, 2.9 are all "2.x"). PHP's version_compare(),
          // on the other hand, treats "x" as a string; so to
          // version_compare(), "2.x" is considered less than 2.0. This
          // means that >=2.x and <2.x are handled by version_compare()
          // as we need, but > and <= are not.
          if ($op == '>' || $op == '<=') {
            $matches['major']++;
          }
          // Equivalence can be checked by adding two restrictions.
          if ($op == '=' || $op == '==') {
            $value['versions'][] = array('op' => '<', 'version' => ($matches['major'] + 1) . '.x');
            $op = '>=';
          }
        }
        $value['versions'][] = array('op' => $op, 'version' => $matches['major'] . '.' . $matches['minor']);
      }
    }
  }
  return $value;
}

/**
 * Checkout a release and determine relevant information.
 *
 * Locate all .info files contained within the project, determine the names of
 * the module(s) contained within the project and store the dependencies for
 * later processing.
 */
function project_release_info_batch_process_release(array $release) {
  $directory_original = getcwd();

  // Use batch directory.
  chdir(PROJECT_RELEASE_INFO_BATCH_DIRECTORY);

  // Clean directory.
  exec('rm -r *');

  // Checkout release.
  $release['tag'] = escapeshellcmd($release['tag']);
  $release['uri'] = escapeshellcmd($release['uri']);

  if ($release['uri'] == 'drupal') {
    $url = ':pserver:anonymous:anonymous@cvs.drupal.org:/cvs/drupal';
    $branch = $release['tag'];
    $module = $release['uri'];
    $directory = $release['uri'];
  }
  else {
    $url = ':pserver:anonymous:anonymous@cvs.drupal.org:/cvs/drupal-contrib';
    $branch = $release['tag'];
    $module = 'contributions/modules/' . $release['uri'];
    $directory = $release['uri'];
  }
  exec("cvs -z6 -Q -d$url checkout -d$directory -r $branch $module", $output, $status);

  // Return to original directory.
  chdir($directory_original);

  // Check for CVS checkout failure.
  if ($status != 0) {
    return FALSE;
  }

  // Scan checkout for .info files and create a list in the same format that
  // project release uses, so that standard API functions can be used.
  $files = file_scan_directory(PROJECT_RELEASE_INFO_BATCH_DIRECTORY . '/' . $directory, '\.info$');
  $info_files = array();
  foreach ($files as $file) {
    $info_files[] = $file->filename;
  }
  $info = project_release_info_package_parse($info_files);

  $dependencies = array();
  foreach ($info as $module => $module_info) {
    $dependencies[$module] = $module_info['dependencies'];
  }

  // Clear previous records for the release.
  project_release_info_package_clear($release['nid']);

  // Store the list of modules contained by the project.
  project_release_info_package_list_store($release['nid'], $info);

  return $dependencies;
}

function project_release_info_batch_process_dependencies($rid, array $component, array $dependencies) {
  package_release_info_process_dependencies($rid, $component, $dependencies);
}

