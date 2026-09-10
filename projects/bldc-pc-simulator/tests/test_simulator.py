import binascii
from dataclasses import replace
import math
import random
import unittest
from simulator.motor import Motor, Parameters, Drive, phase_shape, HALL, PAIRS, TAU
from simulator.controller import DemoPI
from simulator.protocol import Parser, Endpoint, CMD, FB, COMMAND, FEEDBACK, MAX_SIZE, frame, encode_command, decode_feedback


class PlantTests(unittest.TestCase):
    def test_parameter_rejection(self):
        for name, value in [('r_phase', 0), ('l_phase', -1), ('inertia', 0), ('ke_phase', float('nan')),
                            ('vbus', float('inf')), ('pole_pairs', 2.5), ('encoder_cpr', 0), ('sensor_tau', -1)]:
            with self.subTest(name=name), self.assertRaises(ValueError):
                Motor(replace(Parameters(), **{name: value}))

    def test_initial_sensors(self):
        m = Motor()
        self.assertEqual(m.i, [0, 0, 0])
        self.assertEqual(m.snapshot()['hall'], 1)
        self.assertEqual(m.encoder, 0)
        self.assertEqual(m.omega, 0)

    def test_trapezoid_commutation_alignment(self):
        for sector, (pos, neg) in enumerate(PAIRS):
            shapes = phase_shape((sector + 0.5) * TAU / 6)
            self.assertAlmostEqual(shapes[pos], 1)
            self.assertAlmostEqual(shapes[neg], -1)

    def test_locked_rotor_rl_analytic(self):
        m = Motor()
        m.locked = True
        m.step(Drive(.4, 1, True), .1)
        # Two series phases: Req=0.6 ohm, Leq=0.03 H.
        equilibrium = 24 * .4 / .6
        expected = equilibrium * (1 - math.exp(-.6 / .03 * .1))
        self.assertAlmostEqual(m.i[0], expected, delta=.003)
        self.assertAlmostEqual(m.i[1], -m.i[0], places=11)
        self.assertAlmostEqual(m.i[2], 0, places=11)
        self.assertEqual(m.omega, 0)

    def test_zero_and_full_duty(self):
        m = Motor()
        m.locked = True
        m.step(Drive(0, 1, True), .05)
        self.assertEqual(m.i, [0, 0, 0])
        for _ in range(6):
            m.step(Drive(1, 1, True), .1)
        self.assertAlmostEqual(m.i[0], 40, delta=.001)

    def test_kirchhoff_all_sectors(self):
        m = Motor()
        for n in range(60):
            m.step(Drive(.3, n % 6 + 1, True), .001)
            self.assertLess(abs(sum(m.i)), 1e-10)
            self.assertTrue(all(math.isfinite(i) for i in m.i))

    def test_emf_torque_power_identity(self):
        shapes = phase_shape(1.234)
        current = (3.0, -1.2, -1.8)
        for omega in (-200, 0, 200):
            emf = [0.025 * omega * f for f in shapes]
            torque = .025 * sum(i*f for i, f in zip(current, shapes))
            self.assertAlmostEqual(sum(e*i for e, i in zip(emf, current)), torque*omega, places=12)

    def test_disabled_decay_not_reset(self):
        m = Motor()
        m.locked = True
        m.step(Drive(.4, 1, True), .03)
        before = sum(i*i for i in m.i)
        m.step(Drive(), .00005)
        after = sum(i*i for i in m.i)
        self.assertGreater(after, 0)
        self.assertLess(after, before)
        m.step(Drive(), .1)
        self.assertLess(max(abs(i) for i in m.i), 1e-7)

    def test_acceleration_and_reverse(self):
        speeds = []
        for reverse in (False, True):
            m = Motor()
            for _ in range(500):
                m.step(Drive(.25, m.sector, True, reverse), .0002)
            speeds.append(m.omega)
        self.assertGreater(speeds[0], 0)
        self.assertLess(speeds[1], 0)
        self.assertAlmostEqual(speeds[0], -speeds[1], delta=.03)

    def test_hall_and_encoder(self):
        m = Motor()
        for k, expected in enumerate(HALL):
            m.theta = (k + .5) * TAU / 6 / m.p.pole_pairs
            self.assertEqual(m.snapshot()['hall'], expected)
        for count in range(-8, 9):
            m.theta = m.encoder_origin + (count + .2) * TAU / m.p.encoder_cpr
            self.assertEqual(m.encoder, count)
            self.assertEqual(m.snapshot()['encoder_ab'], (0, 1, 3, 2)[count % 4])

    def test_sensor_filter(self):
        m = Motor(replace(Parameters(), sensor_tau=.002, max_step=.00005))
        m.locked = True
        m.step(Drive(.4, 1, True), .00005)
        self.assertAlmostEqual(m.measured, .00005 / .00205 * m.i[0], places=10)

    def test_trip_not_clipping(self):
        m = Motor(replace(Parameters(), current_trip=.5))
        m.locked = True
        m.step(Drive(1, 1, True), .001)
        self.assertTrue(m.fault)
        self.assertNotEqual(m.i[0], .5)

    def test_switching_matches_average(self):
        results = []
        for switched in (False, True):
            m = Motor()
            m.locked = True
            m.step(Drive(.4, 1, True, switched=switched), .005)
            results.append(m.i[0])
        self.assertAlmostEqual(*results, delta=.03)

    def test_step_convergence(self):
        results = []
        for h in (.0001, .00005, .000025):
            m = Motor(replace(Parameters(), max_step=h))
            m.locked = True
            m.step(Drive(.3, 1, True), .02)
            results.append(m.i[0])
        self.assertLess(abs(results[2] - results[1]), abs(results[1] - results[0]))

    def test_pi_three_amp_locked_rotor(self):
        m = Motor()
        m.locked = True
        pi = DemoPI()
        for _ in range(20000):
            m.step(pi.command(m), .0002)
        self.assertAlmostEqual(m.measured, 3, delta=.015)
        self.assertAlmostEqual(pi.duty, .075, delta=.001)

    def test_no_load_does_not_self_start(self):
        m = Motor()
        m.step(Drive(), .1)
        self.assertEqual(m.omega, 0)


class ProtocolTests(unittest.TestCase):
    def payload(self, data):
        packets = Parser().feed(data)
        self.assertEqual(len(packets), 1)
        return packets[0][1]

    def test_crc_reference(self):
        self.assertEqual(binascii.crc_hqx(b'123456789', 0xffff), 0x29b1)

    def test_wire_sizes(self):
        ep = Endpoint(Motor())
        self.assertEqual(len(ep.reply()), 68)
        data = encode_command(0x12345678, ep.session, Drive(.4025, 1, True))
        self.assertEqual(len(data), 30)
        self.assertEqual(data[6:10], bytes.fromhex('78563412'))
        self.assertEqual(CMD.unpack(self.payload(data))[3], 4025)
        self.assertEqual(decode_feedback(self.payload(ep.reply()))['hall'], 1)

    def test_partial_bad_crc_and_resync(self):
        ep = Endpoint(Motor())
        data = encode_command(0, ep.session, Drive())
        parser = Parser()
        self.assertEqual(parser.feed(data[:13]), [])
        self.assertEqual(len(parser.feed(data[13:])), 1)
        bad = bytearray(data)
        bad[-1] ^= 1
        self.assertEqual(parser.feed(bad), [])
        self.assertGreater(parser.errors, 0)
        parser.feed(data[:10])
        self.assertEqual(len(parser.feed(data)), 1)

    def test_parser_bounded(self):
        parser = Parser()
        rng = random.Random(42)
        parser.feed(rng.randbytes(100000))
        self.assertLess(len(parser.buffer), MAX_SIZE)
        self.assertEqual(len(parser.feed(Endpoint(Motor()).reply())), 1)

    def test_duplicate_idempotence_and_sequence(self):
        ep = Endpoint(Motor())
        data = self.payload(encode_command(0xffffffff, ep.session, Drive(.2, 1, True)))
        first = ep.accept(data)
        t = ep.motor.t
        self.assertEqual(ep.accept(data), first)
        self.assertEqual(ep.motor.t, t)
        self.assertEqual(ep.duplicates, 1)
        self.assertIsNotNone(ep.accept(self.payload(encode_command(0, ep.session, Drive()))))
        self.assertIsNone(ep.accept(data))
        self.assertIsNone(ep.accept(self.payload(encode_command(2, ep.session, Drive()))))
        self.assertIsNotNone(ep.accept(self.payload(encode_command(1, ep.session, Drive()))))

    def test_wrong_session_period_reserved(self):
        ep = Endpoint(Motor())
        for args in [(0, ep.session ^ 1, 1000, 0, 1, 0, 0, 0, 3000),
                     (0, ep.session, 10000, 0, 1, 0, 0, 0, 3000),
                     (0, ep.session, 1000, 0, 1, 0, 0, 1, 3000),
                     (0, ep.session, 1000, 0, 1, 0, 0, 0, -1),
                     (0, ep.session, 1000, 0, 1, 0, 0, 0, 20001)]:
            self.assertIsNone(ep.accept(CMD.pack(*args)))
        self.assertEqual(ep.motor.t, 0)

    def test_closed_loop_over_serialization(self):
        ep = Endpoint(Motor())
        ep.motor.locked = True
        integral, duty = 0.0, 0.0
        feedback = decode_feedback(self.payload(ep.reply()))
        # Represents remote STM32: PI at 10 ms, plant exchanges at 1 ms.
        for seq in range(4000):
            if seq % 10 == 0:
                error = 3 - feedback['current_ma'] / 1000
                integral = min(100, max(0, integral + 20 * error * .01))
                duty = min(100, max(0, 2 * error + integral)) / 100
            cmd = self.payload(encode_command(seq, ep.session, Drive(duty, feedback['sector'], True),
                                              target_current=3))
            feedback = decode_feedback(self.payload(ep.accept(cmd)))
        self.assertAlmostEqual(feedback['current_ma'] / 1000, 3, delta=.02)
        self.assertEqual(feedback['time_us'], 4000000)
        self.assertEqual(ep.target_current, 3)


if __name__ == '__main__':
    unittest.main()
