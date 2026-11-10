
-- tables and permissions

drop table wrapped_object CASCADE;
drop sequence wrapped_object_id_seq;
drop table submitter_orole;
drop table orole;
drop table execnode;
drop table submitter;

-- list of roles a script submitter can be acting as

create table orole (
       id    		     character varying(32) PRIMARY KEY,
       max_age		     interval,
       description	     text
       );
grant SELECT on orole to wrapper;
grant SELECT on orole to "wrapper-ro";
revoke all on orole from public;

-- systems and the (operational) roles they allow. (role is a keyword in sql)
-- One row per (hostname_regex, orole) combination.
create table execnode (
       hostname_regex	     text NOT NULL, 
       orole		     character varying(32) REFERENCES orole(id)
       );
grant SELECT on execnode to wrapper;
grant SELECT on execnode to "wrapper-ro";
revoke all on execnode from public;

-- table of known submitters and their contact info
create table submitter (
       login		     character varying(32) PRIMARY KEY,
       name		     character varying(128),
       email		     character varying(350)
       );
grant SELECT on submitter to wrapper;
grant SELECT on submitter to "wrapper-ro";
revoke all on submitter from public;

-- for every role a submitter can act in, there must be a corresponding row in this table

create table submitter_orole (
       login	     	     character varying(32) REFERENCES submitter(login),
       orole     	     character varying(32) REFERENCES orole(id)
       );

grant SELECT on submitter_orole to wrapper;
grant SELECT on submitter_orole to "wrapper-ro";
revoke all on submitter_orole from public;
create unique index submitter_orole_submitter_idx on submitter_orole(login, orole);

-- table of stored object we wrap and their metadata
create sequence wrapped_object_id_seq START WITH 1;
grant all on wrapped_object_id_seq to wrapper;
revoke all on wrapped_object_id_seq from public;

create table wrapped_object (
       object_id 	     integer NOT NULL default nextval('wrapped_object_id_seq'),
       id		     text PRIMARY KEY,
       data	 	     bytea,
       login	     	     character varying(32),
       dsize		     bigint,
       ctime		     timestamp,
       comments		     text,
       chksum		     character varying(64),
       orole	     	     character varying(32),
       -- record_flag values are r=replaced,R=rejected,e=expired
       record_flag	     char
       );

-- TODO: the only update we want to allow is to the record_flag, so
-- it would make sense in a followup to add an trigger to protect the
-- other values or take other protective measures.
grant SELECT, UPDATE, INSERT on wrapped_object to wrapper ;
grant SELECT on wrapped_object to "wrapper-ro";
revoke all on wrapped_object from PUBLIC;

create unique index wrapped_object_id_idx on wrapped_object(id);
create index wrapped_object_chksum_idx on wrapped_object(chksum);


--
-- functions
--

-- on insert, make sure submitter can submit using the given role and
-- mark any object with the same chksum and role as expired. 
CREATE OR REPLACE FUNCTION validate_wrapped_object_insert() RETURNS trigger AS $$
BEGIN
	PERFORM NULL from submitter_orole where login = NEW.login AND orole = NEW.orole;

	IF NOT FOUND THEN
	   RAISE EXCEPTION '% can not submit objects with role %',  NEW.login, NEW.orole;
   	END IF;

	UPDATE wrapped_object SET record_flag = 'r' WHERE record_flag IS NULL AND orole = NEW.orole AND chksum = NEW.chksum;

	RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER legal_submitter BEFORE INSERT on wrapped_object
       FOR EACH ROW EXECUTE PROCEDURE validate_wrapped_object_insert();


-- utility function to convert timestamp to unix time
CREATE OR REPLACE FUNCTION to_epoch(t timestamp) RETURNS int
       AS 'SELECT extract(epoch from $1)::int'
LANGUAGE SQL;


-- get a list of allowed objects based on the hostname of a system
CREATE OR REPLACE FUNCTION export_allowed_objects( thostname text ) RETURNS TABLE ( chksum wrapped_object.chksum%TYPE, id wrapped_object.id%TYPE )
AS $$
BEGIN
	RETURN QUERY SELECT o.chksum, o.id from wrapped_object AS o WHERE o.record_flag IS NULL AND o.orole IN ( SELECT execnode.orole from execnode WHERE thostname ~ execnode.hostname_regex) ORDER BY o.ctime using < ;
END;
$$ LANGUAGE plpgsql;


-- 	 what is the unix time of the newest object that has been added?
CREATE OR REPLACE FUNCTION newest_allowed_object_unixtime ( thostname text ) RETURNS int
AS 'SELECT to_epoch(max(ctime)) from wrapped_object AS o WHERE o.record_flag IS NULL AND o.orole IN ( SELECT execnode.orole from execnode WHERE thostname ~ execnode.hostname_regex)'
LANGUAGE SQL;

-- 
CREATE OR REPLACE FUNCTION seconds_since_last_update ( thostname text ) RETURNS int
AS 'SELECT extract( epoch from (now() - max(ctime)))::int from wrapped_object AS o WHERE o.record_flag IS NULL AND o.orole IN ( SELECT execnode.orole from execnode WHERE thostname ~ execnode.hostname_regex)'
LANGUAGE SQL;


CREATE OR REPLACE FUNCTION newest_id ( thostname text, OUT id text ) AS $$
BEGIN
	SELECT o.id INTO id from wrapped_object AS o WHERE o.record_flag IS NULL AND o.orole IN ( SELECT execnode.orole from execnode WHERE thostname ~ execnode.hostname_regex) ORDER BY ctime USING > LIMIT 1;
	IF NOT FOUND THEN
	   id := 'none';
        END IF;	
END;
$$ LANGUAGE plpgsql;

--	 is thostname allowed to run a script with given checksum?
CREATE OR REPLACE FUNCTION validate_chksum( thostname text, tchksum character(64),
       	  	  	       OUT status int, OUT id text, OUT message text ) AS $$
DECLARE
	o1 wrapped_object%ROWTYPE;
BEGIN
	SELECT * INTO o1 FROM wrapped_object WHERE chksum = tchksum AND record_flag IS NULL AND orole in (select en.orole from execnode AS en where thostname ~ en.hostname_regex) ORDER BY ctime using < LIMIT 1;

	IF NOT FOUND THEN
	   id := NULL;
	   status := 1;
	   message := 'no active object with chksum ' || tchksum || ' found in database';
	   RETURN;
	END IF;

	id := o1.id;
	status := 0;
	message := 'ID ' || o1.id || ' is allowed on ' || thostname;
END;
$$ LANGUAGE plpgsql;


--  A script should periodically call this to expire objects based on the max_age of its orole
CREATE OR REPLACE FUNCTION expire_objects () RETURNS void AS $$
BEGIN
	UPDATE wrapped_object SET record_flag='e' WHERE record_flag IS NULL AND 'now' - ctime > ( SELECT max_age FROM orole WHERE wrapped_object.orole = orole.id );
END;
$$ LANGUAGE plpgsql;


 
