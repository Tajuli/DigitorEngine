import 'package:flutter/material.dart';
void main()=>runApp(const MaterialApp(home:DigitorExample()));
class DigitorExample extends StatelessWidget{const DigitorExample({super.key});@override Widget build(BuildContext context)=>Scaffold(appBar:AppBar(title:const Text('Digitor Preview')),body:const Center(child:Text('Native texture preview ready')));}
