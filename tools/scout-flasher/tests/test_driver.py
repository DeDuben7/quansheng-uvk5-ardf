import json
import subprocess

from sf.driver import Cable, driver_problem, memory_integrity_on, needs_fix, query_cables


def fake_run(payload):
    def run(cmd, **kwargs):
        return subprocess.CompletedProcess(cmd, 0, stdout=json.dumps(payload), stderr="")
    return run


PL2303_OK = {
    "InstanceId": "USB\\VID_067B&PID_2303\\5&D436FBD&0&2",
    "FriendlyName": "Prolific USB-to-Serial Comm Port (COM9)",
    "Status": "OK", "Problem": 0, "DriverVersion": "3.3.11.152",
    "HardwareIds": ["USB\\VID_067B&PID_2303&REV_0300", "USB\\VID_067B&PID_2303"],
}
PL2303_PHASED_OUT = dict(PL2303_OK, Status="Error", Problem=10, DriverVersion="3.9.6.0",
                         FriendlyName="Prolific PL2303HXA PHASED OUT SINCE 2012. PLEASE CONTACT YOUR SUPPLIER.")
CH340 = {
    "InstanceId": "USB\\VID_1A86&PID_7523\\5&1",
    "FriendlyName": "USB-SERIAL CH340 (COM4)",
    "Status": "OK", "Problem": 0, "DriverVersion": "3.8.2023.2",
    "HardwareIds": ["USB\\VID_1A86&PID_7523&REV_0264", "USB\\VID_1A86&PID_7523"],
}


def test_query_parses_single_object():
    # ConvertTo-Json returns a bare object, not a list, for one device.
    cables = query_cables(run=fake_run(PL2303_OK))
    assert cables == [Cable(
        instance_id=PL2303_OK["InstanceId"], vid="067B", pid="2303", rev="0300",
        status="OK", problem=0, driver_version="3.3.11.152", com_port="COM9",
        friendly_name=PL2303_OK["FriendlyName"])]


def test_query_parses_list_and_no_com_port():
    cables = query_cables(run=fake_run([PL2303_PHASED_OUT, CH340]))
    assert [c.vid for c in cables] == ["067B", "1A86"]
    assert cables[0].com_port is None
    assert cables[0].problem == 10
    assert cables[1].com_port == "COM4"


def test_query_no_devices():
    def run(cmd, **kwargs):
        return subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")
    assert query_cables(run=run) == []


def test_needs_fix_only_for_wrong_driver():
    ok, bad, ch = query_cables(run=fake_run([PL2303_OK, PL2303_PHASED_OUT, CH340]))
    assert not needs_fix(ok) and not driver_problem(ok)
    assert needs_fix(bad) and not driver_problem(bad)
    assert not needs_fix(ch) and not driver_problem(ch)
    # Another PL2303 revision is not ours to change.
    assert not needs_fix(Cable(**dict(bad.__dict__, rev="0400")))


def test_right_driver_with_problem_is_not_offered_the_fix_again():
    # e.g. blocked by Memory Integrity, or needs a restart after the fix
    ok = query_cables(run=fake_run(PL2303_OK))[0]
    stuck = Cable(**dict(ok.__dict__, status="Error", problem=39))
    assert driver_problem(stuck)
    assert not needs_fix(stuck)


def test_memory_integrity_parsing():
    def run_with(out):
        return lambda cmd, **kw: subprocess.CompletedProcess(cmd, 0, stdout=out, stderr="")
    assert memory_integrity_on(run=run_with("True\r\n")) is True
    assert memory_integrity_on(run=run_with("False")) is False
    assert memory_integrity_on(run=run_with("")) is None
