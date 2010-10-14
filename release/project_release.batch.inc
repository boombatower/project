<?php
// $Id$

/**
 * @file
 * Parses a batch of releases to build project release info tables.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Directory to checkout modules during processing.
 *
 * The directory should be empty since it will be cleared before each checkout.
 */
define('PROJECT_RELEASE_INFO_BATCH_DIRECTORY', '/tmp/project-info');

/**
 * Load Drupal implementation by default.
 */
module_load_include('drupal.inc', 'project_release');

/**
 * Legacy parsing form.
 *
 * Provide a form to select the API versions that releases must be compatible
 * with and initiate the batch process to perform the parsing.
 */
function project_release_info_batch_form(&$form_state) {
  // Ensure that the directory constant has been set.
  if (empty(PROJECT_RELEASE_INFO_BATCH_DIRECTORY)) {
    form_set_error('', t('The constant PROJECT_RELEASE_INFO_BATCH_DIRECTORY must be set.'));
  }

  $api_vid = _project_release_get_api_vid();
  $form['filter'] = array(
    '#type' => 'fieldset',
    '#title' => t('Filter'),
  );
  $form['filter']['api'] = array(
    '#type' => 'checkboxes',
    '#title' => t('API'),
    '#description' => t('Select the API versions that releases must be compatible with.'),
    '#options' => array(),
    '#default_value' => array(),
  );
  $terms = project_release_get_api_taxonomy();
  foreach ($terms as $term) {
    $form['filter']['api']['#options'][$term->tid] = $term->name;
    // Defaultly select
    if (($major = array_shift(explode('.', $term->name, 2))) && $major >= PROJECT_RELEASE_INFO_MINIMUM_API) {
      $form['filter']['api']['#default_value'][] = $term->tid;
    }
  }
  $form['op'] = array('#type' => 'submit', '#value' => t('Submit'));

  return $form;
}

/**
 * Start the batch to process batch releases.
 */
function project_release_info_batch_form_submit($form, &$form_state) {
  $tids = array_keys(array_filter($form_state['values']['api']));

  $batch = array(
    'title' => t('Processing releases'),
    'operations' => array(
      array('project_release_info_batch_batch_operation_parse', array($tids)),
      array('project_release_info_batch_batch_operation_dependencies', array()),
    ),
    'finished' => 'project_release_info_batch_batch_finished',
    'init_message' => t('Determining releases to parse...'),
    'file' => drupal_get_path('module', 'project_release_info') . '/project_release_info.batch.inc',
  );
  batch_set($batch);
}

/**
 * Parse the modules contained within a project and their dependencies.
 */
function project_release_info_batch_batch_operation_parse(array $tids, &$context) {
  if (!isset($context['sandbox']['max'])) {
    // First iteration, initialize working values.
    $context['sandbox']['max'] = project_release_info_batch_releases($tids, 0, TRUE);
    $context['sandbox']['release_id'] = 0;
    $context['sandbox']['progress'] = 0;
    $context['results']['dependencies'] = array();
    $context['results']['count'] = 0;
  }

  // Load the package include that contains functions to save and parse data.
  module_load_include('package.inc', 'project_release');

  $releases = project_release_info_batch_releases($tids, $context['sandbox']['release_id']);
  foreach ($releases as $release) {
    if (($dependencies = project_release_info_batch_process_release($release)) === FALSE) {
      $context['results']['error'] = $release;
      $context['finished'] = 1;
      return;
    }
    $context['results']['dependencies'][$release['nid']] = $dependencies;
    $context['sandbox']['release_id'] = $release['nid'];
    $context['sandbox']['progress']++;
    $context['results']['count']++;
  }

  $context['message'] = t('Last parsed %module release %tag (@progress of @max).', array(
    '%module' => $release['uri'],
    '%tag' => $release['tag'],
    '@progress' => number_format($context['sandbox']['progress']),
    '@max' => number_format($context['sandbox']['max']),
  ));

  $context['finished'] = $context['sandbox']['progress'] / $context['sandbox']['max'];
}

/**
 * Determine dependencies of releases.
 */
function project_release_info_batch_batch_operation_dependencies(&$context) {
  if (!isset($context['sandbox']['max'])) {
    $context['sandbox']['max'] = count($context['results']['dependencies']);
    $context['sandbox']['progress'] = 0;
  }

  // Load the package include that contains functions to save and parse data.
  module_load_include('package.inc', 'project_release');

  $i = 0;
  foreach ($context['results']['dependencies'] as $rid => $modules_dependencies) {
    $modules = project_release_info_module_list_get($rid);
    foreach ($modules_dependencies as $module => $dependencies) {
      project_release_info_batch_process_dependencies($rid, $modules[$module], $dependencies);
    }

    // Remove already processed releases.
    unset($context['results']['dependencies'][$rid]);

    $context['sandbox']['progress']++;

    // Only process 20 releases in a single batch operation.
    if ($i++ == 20) {
      break;
    }
  }

  $context['message'] = t('Processed @progress of @max release dependencies.', array(
    '@progress' => number_format($context['sandbox']['progress']),
    '@max' => number_format($context['sandbox']['max']),
  ));

  $context['finished'] = $context['sandbox']['progress'] / $context['sandbox']['max'];
}

/**
 * Display relevant finished message after batch run.
 */
function project_release_info_batch_batch_finished($success, $results, $operations) {
  if ($success && !isset($results['error'])) {
    drupal_set_message(t('Processed @count release(s).', array(
      '@count' => number_format($results['count']),
    )));
  }
  else {
    drupal_set_message(t('Failed to process @module release #@rid.', array(
      '@module' => $results['error']['uri'],
      '@rid' => $results['error']['nid'],
    )), 'error');
  }
}

/**
 * Select all releases that are compatible with the API tids.
 *
 * @return
 *   List of release information: rid, project uri, and cvs directory.
 */
function project_release_info_batch_releases(array $tids, $release_id, $count = FALSE) {
  $fields = ($count ? 'COUNT(r.nid)' : 'r.nid, r.tag, p.uri, cp.directory');
  $placeholders = db_placeholders($tids, 'int');
  $sql = "SELECT $fields
    FROM {project_release_nodes} r
    INNER JOIN {node} n ON r.nid = n.nid
    INNER JOIN {term_node} t ON n.vid = t.vid
    INNER JOIN {project_projects} p ON r.pid = p.nid
    INNER JOIN {cvs_projects} cp ON p.nid = cp.nid
    WHERE t.tid IN ($placeholders) AND r.nid > %d";
  $args = array_merge($tids, array($release_id));

  if ($count) {
    return db_result(db_query($sql, $args));
  }

  $sql .= ' ORDER BY r.nid';
  $result = db_query_range($sql, $args, 0, 10);
  $releases = array();
  while ($release = db_fetch_array($result)) {
    $releases[] = $release;
  }
  return $releases;
}

