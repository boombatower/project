-- $Id$

ALTER TABLE project_issues RENAME state TO sid;
ALTER TABLE project_issues ALTER COLUMN sid SET smallint;
ALTER TABLE project_issues ALTER COLUMN sid SET NOT NULL;
ALTER TABLE project_issues ALTER COLUMN sid SET DEFAULT 0;


CREATE TABLE project_issue_state (
  sid SERIAL,
  name varchar(32) NOT NULL default '',
  weight smallint DEFAULT '0' NOT NULL,
  author_has smallint DEFAULT '0' NOT NULL,
  PRIMARY KEY  (sid)
) TYPE=MyISAM;

--
-- Data for table 'project_issue_state'
--

INSERT INTO project_issue_state VALUES (1, 'active', -13, 0);
INSERT INTO project_issue_state VALUES (2, 'applied', 1, 0);
INSERT INTO project_issue_state VALUES (3, 'duplicate', 4, 0);
INSERT INTO project_issue_state VALUES (4, 'postponed', 6, 0);
INSERT INTO project_issue_state VALUES (5, 'won\'t fix', 9, 0);
INSERT INTO project_issue_state VALUES (6, 'by design', 11, 0);
INSERT INTO project_issue_state VALUES (7, 'closed', 13, 1);
INSERT INTO project_issue_state VALUES (8, 'patch', -8, 0);
INSERT INTO project_issue_state VALUES (9, 'needs work', -11, 0);
INSERT INTO project_issue_state VALUES (10, 'testers needed', -6, 0);
INSERT INTO project_issue_state VALUES (11, 'reviewed', -3, 0);
INSERT INTO project_issue_state VALUES (12, 'ready to commit', -1, 0);